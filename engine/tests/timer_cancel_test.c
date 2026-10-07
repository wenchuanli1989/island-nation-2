#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <threads.h>
#include <unistd.h>

#include "domino_shared_common.h"
#include "time/entry.h"
#include "timer/internal.h"

#define TEST_QUEUE_COUNT 2U
#define TEST_EVENT_CAPACITY 8U
#define TEST_WAIT_NS UINT64_C(2000000000)

#define TEST_CHECK(condition, message)                              \
    do {                                                            \
        if (!(condition)) {                                         \
            (void)fprintf(stderr, "%s: %s\n", __func__, (message)); \
            goto cleanup;                                           \
        }                                                           \
    } while (0)

typedef struct TimerDispatchState {
    DominoTimer events[TEST_EVENT_CAPACITY];
    uint32_t dispatch_count;
    DOMINO_CODE self_cancel_code;
    bool cancel_self;
    bool overflow;
} TimerDispatchState;

typedef struct TimerFixture {
    DominoThreadQueue queues[TEST_QUEUE_COUNT];
    TimerDispatchState states[TEST_QUEUE_COUNT];
} TimerFixture;

static void recordTimerEvent(const DominoTimer* event_ptr, TimerDispatchState* state_ptr) {
    if (state_ptr->cancel_self) {
        state_ptr->self_cancel_code = dominoTimerCancel(event_ptr->timer_id, event_ptr->event_queue_ptr);
    }
    if (state_ptr->dispatch_count == TEST_EVENT_CAPACITY) {
        state_ptr->overflow = true;
        return;
    }
    state_ptr->events[state_ptr->dispatch_count++] = *event_ptr;
}

static bool startFixture(TimerFixture* fixture_ptr, uint32_t queue_capacity) {
    dominoTimeModuleInitBefore();
    for (uint32_t i = 0U; i < TEST_QUEUE_COUNT; ++i) {
        if (dominoThreadQueueInit(&fixture_ptr->queues[i], sizeof(DominoTimer), queue_capacity, queue_capacity) != CODE_OK) {
            return false;
        }
    }
    return dominoTimerModuleInit() == CODE_OK;
}

/** @brief 批量登记期间保持累计运行时间冻结；准备好后只解冻一次并唤醒投递线程。 */
static bool startDelivery(void) {
    if (dominoTimeModuleIsFrozen()) {
        dominoTimeModuleInitAfter();
    }
    dominoTimerNotifyTimeChanged();
    return !dominoTimeModuleIsFrozen();
}

static void destroyFixture(TimerFixture* fixture_ptr) {
    dominoTimerModuleExit();
    dominoTimeModuleExit();
    for (uint32_t i = 0U; i < TEST_QUEUE_COUNT; ++i) {
        dominoThreadQueueDestroy(&fixture_ptr->queues[i]);
    }
}

/** @brief 创建业务请求；登记后通过同一结构体的 timer_id 取消。 */
static DominoTimer createTimerRequest(DominoThreadQueue* queue_ptr) {
    return (DominoTimer){.event_queue_ptr = queue_ptr, .event_type = 3U};
}

/** @brief 业务自行出队并检查取消标记，不取得 timer 请求锁。 */
static DOMINO_CODE consumeNextEvent(DominoThreadQueue* queue_ptr, TimerDispatchState* state_ptr) {
    DominoTimer timer;
    DOMINO_CODE code = dominoThreadQueueConsume(queue_ptr, &timer, false);
    if (code == CODE_OK && !timer.cancelled_flag) {
        recordTimerEvent(&timer, state_ptr);
    }
    return code;
}

static bool waitForQueuedEvents(DominoThreadQueue* queue_ptr, uint32_t event_count) {
    uint64_t start_ns = dominoMonotonicTimeNs();
    while (dominoMonotonicTimeNs() - start_ns < TEST_WAIT_NS) {
        checkThreadResult(mtx_lock(&queue_ptr->mutex));
        bool ready_flag = queue_ptr->count == event_count;
        checkThreadResult(mtx_unlock(&queue_ptr->mutex));
        if (ready_flag) {
            return true;
        }
        thrd_yield();
    }
    return false;
}

/** @brief 在请求锁内检查堆及命令队列，排除尚未迁移的命令造成的短暂计数吻合。 */
static bool waitForHeapEvents(uint32_t event_count) {
    uint64_t start_ns = dominoMonotonicTimeNs();
    while (dominoMonotonicTimeNs() - start_ns < TEST_WAIT_NS) {
        checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
        checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
        bool ready_flag = g_domino_timer.heap.count == event_count && g_domino_timer.command_queue.count == 0U;
        checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
        if (ready_flag) {
            return true;
        }
        thrd_yield();
    }
    return false;
}

/** @brief 成功消费计数包含被取消而跳过分派的事件。 */
static bool consumeEvents(TimerFixture* fixture_ptr, uint32_t queue_index, uint32_t event_count) {
    uint64_t start_ns = dominoMonotonicTimeNs();
    uint32_t consumed_count = 0U;
    while (consumed_count < event_count && dominoMonotonicTimeNs() - start_ns < TEST_WAIT_NS) {
        DOMINO_CODE code = consumeNextEvent(&fixture_ptr->queues[queue_index], &fixture_ptr->states[queue_index]);
        if (code == CODE_OK) {
            consumed_count++;
        } else if (code != ERR_QUEUE_EMPTY) {
            return false;
        }
        thrd_yield();
    }
    return consumed_count == event_count;
}

static bool testCancelBeforeDelivery(void) {
    bool success_flag = false;
    TimerFixture fixture = {0};
    DominoTimer requests[3] = {createTimerRequest(&fixture.queues[0]), createTimerRequest(&fixture.queues[0]),
                               createTimerRequest(&fixture.queues[0])};
    TEST_CHECK(dominoTimerCancel(1U, &fixture.queues[0]) == ERR_NOT_INITIALIZED, "cancel requires an initialized module");
    TEST_CHECK(startFixture(&fixture, 3U), "initialize accepting timer with frozen runtime");
    TEST_CHECK(dominoTimerCancel(0U, &fixture.queues[0]) == ERR_INVALID_PARAM, "zero is not a timer ID");
    TEST_CHECK(dominoTimerCancel(UINT64_MAX, &fixture.queues[0]) == ERR_NOT_FOUND, "unknown timer ID is rejected");
    TEST_CHECK(dominoTimerSchedule(&requests[0]) == CODE_OK && dominoTimerSchedule(&requests[1]) == CODE_OK,
               "schedule events sharing the same business payload");
    TEST_CHECK(dominoTimerCancel(requests[0].timer_id, &fixture.queues[1]) == ERR_NOT_FOUND,
               "the same ID cannot cancel a request belonging to another business queue");
    TEST_CHECK(dominoTimerCancel(requests[0].timer_id, &fixture.queues[0]) == CODE_OK, "cancel an accepted event before runtime is thawed");
    TEST_CHECK(dominoTimerCancel(requests[0].timer_id, &fixture.queues[0]) == ERR_NOT_FOUND, "a second cancellation cannot claim the same ID");
    TEST_CHECK(dominoTimerSchedule(&requests[2]) == CODE_OK && requests[0].timer_id == 1U && requests[1].timer_id == 2U && requests[2].timer_id == 3U,
               "successful and failed cancellations consume no command IDs");
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    bool retained_flag = g_domino_timer.command_queue.count == 3U && g_domino_timer.heap.count == 0U;
    checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    TEST_CHECK(retained_flag && dominoTimeModuleIsFrozen(), "frozen timer retains every command, including the marked cancellation");
    TEST_CHECK(startDelivery() && consumeEvents(&fixture, 0U, 2U), "only uncancelled requests reach the business queue");
    const TimerDispatchState* state_ptr = &fixture.states[0];
    TEST_CHECK(state_ptr->dispatch_count == 2U && !state_ptr->overflow && state_ptr->events[0].timer_id == requests[1].timer_id &&
                   state_ptr->events[1].timer_id == requests[2].timer_id,
               "only the cancelled ID is skipped while sibling events execute in order");
    TEST_CHECK(state_ptr->events[0].event_queue_ptr == &fixture.queues[0] && state_ptr->events[1].event_queue_ptr == &fixture.queues[0] &&
                   state_ptr->events[0].delay_ms == 0U && state_ptr->events[1].delay_ms == 0U && state_ptr->events[0].event_type == 3U &&
                   state_ptr->events[1].event_type == 3U,
               "cancellation preserves the other events' business payload");
    TEST_CHECK(dominoTimerCancel(requests[1].timer_id, &fixture.queues[0]) == ERR_NOT_FOUND, "already dispatched event is no longer cancellable");
    TEST_CHECK(consumeNextEvent(&fixture.queues[0], &fixture.states[0]) == ERR_QUEUE_EMPTY, "cancelled request never occupies a business queue slot");
    dominoTimerModuleExit();
    TEST_CHECK(dominoTimerCancel(requests[2].timer_id, &fixture.queues[0]) == ERR_NOT_INITIALIZED, "module exit removes the cancellation service");
    success_flag = true;

cleanup:
    destroyFixture(&fixture);
    return success_flag;
}

/** @brief 未来项已入堆后冻结并取消；恢复处理后确认取消项已回收且不投递。 */
static bool testCancelInHeap(void) {
    bool success_flag = false;
    TimerFixture fixture = {0};
    DominoTimer request = {.event_queue_ptr = &fixture.queues[0], .delay_ms = UINT32_C(60000)};
    TEST_CHECK(startFixture(&fixture, 1U), "initialize timer with frozen runtime for a future deadline");
    TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK && startDelivery() && waitForHeapEvents(1U),
               "accepted future request reaches the global heap after thaw");
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    dominoTimeModuleExit();
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    dominoTimerNotifyTimeChanged();
    TEST_CHECK(dominoTimerCancel(request.timer_id, &fixture.queues[1]) == ERR_NOT_FOUND, "heap cancellation requires the registered business queue");
    TEST_CHECK(dominoTimerCancel(request.timer_id, &fixture.queues[0]) == CODE_OK &&
                   dominoTimerCancel(request.timer_id, &fixture.queues[0]) == ERR_NOT_FOUND,
               "cancel a known heap request once");
    TEST_CHECK(dominoTimeModuleIsFrozen(), "cancellation keeps runtime frozen");
    TEST_CHECK(startDelivery() && waitForHeapEvents(0U), "cancelled heap item is reclaimed before its deadline when processing resumes");
    TEST_CHECK(consumeNextEvent(&fixture.queues[0], &fixture.states[0]) == ERR_QUEUE_EMPTY && fixture.states[0].dispatch_count == 0U,
               "reclaimed future request is never delivered or dispatched");
    success_flag = true;

cleanup:
    destroyFixture(&fixture);
    return success_flag;
}

static bool testCancelQueuedDuringDelivery(void) {
    bool success_flag = false;
    TimerFixture fixture = {0};
    DominoTimer requests[4] = {createTimerRequest(&fixture.queues[0]), createTimerRequest(&fixture.queues[0]), createTimerRequest(&fixture.queues[0]),
                               createTimerRequest(&fixture.queues[0])};
    TEST_CHECK(startFixture(&fixture, 2U), "initialize timer for queued cancellation");
    TEST_CHECK(dominoTimerSchedule(&requests[0]) == CODE_OK && dominoTimerSchedule(&requests[1]) == CODE_OK, "schedule first queued pair");
    TEST_CHECK(startDelivery() && waitForQueuedEvents(&fixture.queues[0], 2U), "both events reach the business queue");
    TEST_CHECK(dominoTimerCancel(requests[0].timer_id, &fixture.queues[1]) == ERR_NOT_FOUND,
               "queued cancellation cannot affect an event in another business queue");
    dominoTimeModuleExit();
    dominoTimerNotifyTimeChanged();
    TEST_CHECK(dominoTimeModuleIsFrozen() && dominoTimerCancel(requests[0].timer_id, &fixture.queues[0]) == CODE_OK,
               "cancel a queued event while runtime is frozen");
    TEST_CHECK(
        consumeEvents(&fixture, 0U, 2U) && fixture.states[0].dispatch_count == 1U && fixture.states[0].events[0].timer_id == requests[1].timer_id,
        "frozen time still permits consuming queued events and skips only the cancelled request");
    TEST_CHECK(dominoTimerSchedule(&requests[2]) == CODE_OK && dominoTimerSchedule(&requests[3]) == CODE_OK, "schedule another queued pair");
    TEST_CHECK(startDelivery() && waitForQueuedEvents(&fixture.queues[0], 2U), "both new events reach the business queue");
    TEST_CHECK(dominoTimerCancel(requests[2].timer_id, &fixture.queues[0]) == CODE_OK &&
                   dominoTimerCancel(requests[2].timer_id, &fixture.queues[0]) == ERR_NOT_FOUND,
               "cancel an unclaimed queued event while the worker remains active");
    TEST_CHECK(
        consumeEvents(&fixture, 0U, 2U) && fixture.states[0].dispatch_count == 2U && fixture.states[0].events[1].timer_id == requests[3].timer_id,
        "dispatch skips cancelled events and claims active events during delivery");
    TEST_CHECK(dominoTimerCancel(requests[3].timer_id, &fixture.queues[0]) == ERR_NOT_FOUND &&
                   consumeNextEvent(&fixture.queues[0], &fixture.states[0]) == ERR_QUEUE_EMPTY,
               "consumption removes the active ID exactly once");
    success_flag = true;

cleanup:
    destroyFixture(&fixture);
    return success_flag;
}

/** @brief 子进程投递超过事件队列容量时必须终止，并输出准确的失败请求诊断。 */
static bool testFullQueueAbort(void) {
    bool success_flag = false;
    TimerFixture fixture = {0};
    FILE* diagnostic_file_ptr = nullptr;
    TEST_CHECK(!g_domino_timer.initialized_flag, "fork only after the previous timer worker has exited");
    diagnostic_file_ptr = tmpfile();
    TEST_CHECK(diagnostic_file_ptr != nullptr, "create a file for the child diagnostic");
    pid_t child_pid = fork();
    TEST_CHECK(child_pid >= 0, "create a child for the fatal delivery scenario");
    if (child_pid == 0) {
        if (dup2(fileno(diagnostic_file_ptr), STDERR_FILENO) < 0) {
            _Exit(EXIT_FAILURE);
        }
        DominoTimer requests[2] = {createTimerRequest(&fixture.queues[0]), createTimerRequest(&fixture.queues[0])};
        if (!startFixture(&fixture, 1U) || dominoTimerSchedule(&requests[0]) != CODE_OK || dominoTimerSchedule(&requests[1]) != CODE_OK ||
            requests[0].timer_id != 1U || requests[1].timer_id != 2U || !startDelivery()) {
            _Exit(EXIT_FAILURE);
        }
        // 不消费事件；旧等待重试策略会在超时后以失败退出，不能使父进程永久等待。
        uint64_t start_ns = dominoMonotonicTimeNs();
        while (dominoMonotonicTimeNs() - start_ns < TEST_WAIT_NS) {
            thrd_yield();
        }
        _Exit(EXIT_FAILURE);
    }
    int child_status = 0;
    pid_t waited_pid;
    do {
        waited_pid = waitpid(child_pid, &child_status, 0);
    } while (waited_pid < 0 && errno == EINTR);
    TEST_CHECK(waited_pid == child_pid, "reap the fatal delivery child");
    TEST_CHECK(WIFSIGNALED(child_status) && WTERMSIG(child_status) == SIGABRT, "full event queue terminates the child with SIGABRT");
    TEST_CHECK(fseek(diagnostic_file_ptr, 0L, SEEK_SET) == 0, "rewind the child diagnostic");
    char diagnostic_buf[512];
    size_t diagnostic_len = fread(diagnostic_buf, 1U, sizeof(diagnostic_buf) - 1U, diagnostic_file_ptr);
    diagnostic_buf[diagnostic_len] = '\0';
    TEST_CHECK(!ferror(diagnostic_file_ptr), "read the child diagnostic");
    char expected_diagnostic[128];
    (void)snprintf(expected_diagnostic, sizeof(expected_diagnostic), "domino timer: delivery to queue %p, timer 2 failed (%d)\n",
                   (void*)&fixture.queues[0], ERR_QUEUE_FULL);
    TEST_CHECK(strstr(diagnostic_buf, expected_diagnostic) != nullptr, "fatal diagnostic identifies the full business queue, timer and queue error");
    success_flag = true;

cleanup:
    if (diagnostic_file_ptr != nullptr) {
        (void)fclose(diagnostic_file_ptr);
    }
    return success_flag;
}

/** @brief 业务出队后再次取消自身，验证出队已认领事件且业务处理不持队列锁。 */
static bool testCancelClaimedEvent(void) {
    bool success_flag = false;
    TimerFixture fixture = {0};
    TEST_CHECK(startFixture(&fixture, 1U), "initialize timer for reentrant cancellation");
    fixture.states[0].cancel_self = true;
    DominoTimer request = createTimerRequest(&fixture.queues[0]);
    TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK && startDelivery() && consumeEvents(&fixture, 0U, 1U),
               "dispatch can call the cancellation API without deadlocking");
    TEST_CHECK(fixture.states[0].dispatch_count == 1U && fixture.states[0].events[0].timer_id == request.timer_id &&
                   fixture.states[0].self_cancel_code == ERR_NOT_FOUND,
               "an event already claimed for dispatch cannot cancel itself");
    TEST_CHECK(dominoTimerCancel(request.timer_id, &fixture.queues[0]) == ERR_NOT_FOUND, "claimed event remains unavailable after dispatch returns");
    success_flag = true;

cleanup:
    destroyFixture(&fixture);
    return success_flag;
}

typedef struct TimerCancelRace {
    domino_timer_id_t timer_ids[TEST_EVENT_CAPACITY];
    DominoThreadQueue* queue_ptr;
    TimerDispatchState* state_ptr;
    DOMINO_CODE cancel_codes[TEST_EVENT_CAPACITY];
    DOMINO_CODE consume_codes[TEST_EVENT_CAPACITY];
    _Atomic bool start_flag;
} TimerCancelRace;

static int runCancelRace(void* argument_ptr) {
    TimerCancelRace* race_ptr = argument_ptr;
    while (!atomic_load_explicit(&race_ptr->start_flag, memory_order_acquire)) {
        thrd_yield();
    }
    for (uint32_t i = 0U; i < TEST_EVENT_CAPACITY; ++i) {
        race_ptr->cancel_codes[i] = dominoTimerCancel(race_ptr->timer_ids[i], race_ptr->queue_ptr);
    }
    return 0;
}

static int runConsumeRace(void* argument_ptr) {
    TimerCancelRace* race_ptr = argument_ptr;
    while (!atomic_load_explicit(&race_ptr->start_flag, memory_order_acquire)) {
        thrd_yield();
    }
    for (uint32_t i = 0U; i < TEST_EVENT_CAPACITY; ++i) {
        race_ptr->consume_codes[i] = consumeNextEvent(race_ptr->queue_ptr, race_ptr->state_ptr);
    }
    return 0;
}

/** @brief 取消与消费竞争同一批已排队事件；每个 ID 只能由其中一方成功认领。 */
static bool testConcurrentCancelAndConsume(void) {
    bool success_flag = false;
    TimerFixture fixture = {0};
    TimerCancelRace race = {.queue_ptr = &fixture.queues[0], .state_ptr = &fixture.states[0]};
    thrd_t threads[2];
    uint32_t thread_count = 0U;
    atomic_init(&race.start_flag, false);
    TEST_CHECK(startFixture(&fixture, TEST_EVENT_CAPACITY), "initialize timer for concurrent cancellation and business consumption");
    DominoTimer request = createTimerRequest(&fixture.queues[0]);
    for (uint32_t i = 0U; i < TEST_EVENT_CAPACITY; ++i) {
        TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK, "schedule the bounded race batch");
        race.timer_ids[i] = request.timer_id;
    }
    TEST_CHECK(startDelivery() && waitForQueuedEvents(&fixture.queues[0], TEST_EVENT_CAPACITY),
               "all race events are queued before cancellation and consumption compete");
    TEST_CHECK(thrd_create(&threads[thread_count], runCancelRace, &race) == thrd_success, "create cancellation thread");
    thread_count++;
    TEST_CHECK(thrd_create(&threads[thread_count], runConsumeRace, &race) == thrd_success, "create business consumer thread");
    thread_count++;
    atomic_store_explicit(&race.start_flag, true, memory_order_release);
    for (uint32_t i = 0U; i < thread_count; ++i) {
        checkThreadResult(thrd_join(threads[i], nullptr));
    }
    thread_count = 0U;
    const TimerDispatchState* state_ptr = &fixture.states[0];
    TEST_CHECK(!state_ptr->overflow, "race dispatch does not exceed the submitted batch");
    for (uint32_t i = 0U; i < TEST_EVENT_CAPACITY; ++i) {
        TEST_CHECK(race.consume_codes[i] == CODE_OK, "each queued event is consumed even if cancellation won");
        TEST_CHECK(race.cancel_codes[i] == CODE_OK || race.cancel_codes[i] == ERR_NOT_FOUND,
                   "each cancellation either wins or observes a prior claim");
        uint32_t matching_count = 0U;
        for (uint32_t j = 0U; j < state_ptr->dispatch_count; ++j) {
            matching_count += state_ptr->events[j].timer_id == race.timer_ids[i] ? 1U : 0U;
        }
        uint32_t expected_count = race.cancel_codes[i] == CODE_OK ? 0U : 1U;
        TEST_CHECK(matching_count == expected_count, "successful cancel prevents dispatch; a lost cancellation race dispatches exactly once");
    }
    TEST_CHECK(consumeNextEvent(&fixture.queues[0], &fixture.states[0]) == ERR_QUEUE_EMPTY,
               "race consumes the complete batch and leaves the queue empty");
    success_flag = true;

cleanup:
    atomic_store_explicit(&race.start_flag, true, memory_order_release);
    for (uint32_t i = 0U; i < thread_count; ++i) {
        checkThreadResult(thrd_join(threads[i], nullptr));
    }
    destroyFixture(&fixture);
    return success_flag;
}

/** @brief 业务拥有事件队列；timer 退出保留已投递事件，业务排空后才能安全复用新生命周期的 ID。 */
static bool testBusinessQueueLifecycle(void) {
    bool success_flag = false;
    TimerFixture fixture = {0};
    DominoTimer old_request = createTimerRequest(&fixture.queues[0]);
    TEST_CHECK(startFixture(&fixture, 1U), "initialize original timer lifecycle with a business queue");
    TEST_CHECK(dominoTimerSchedule(&old_request) == CODE_OK && startDelivery() && waitForQueuedEvents(&fixture.queues[0], 1U),
               "leave a real timer event queued before module exit");
    dominoTimerModuleExit();
    TEST_CHECK(!g_domino_timer.initialized_flag && fixture.queues[0].initialized && fixture.queues[0].items_ptr != nullptr &&
                   fixture.queues[0].count == 1U && fixture.states[0].dispatch_count == 0U,
               "timer exit preserves the business queue and its delivered event");
    TEST_CHECK(dominoTimerCancel(old_request.timer_id, &fixture.queues[0]) == ERR_NOT_INITIALIZED,
               "module exit removes the cancellation service without changing the business queue");
    TEST_CHECK(
        consumeEvents(&fixture, 0U, 1U) && fixture.states[0].dispatch_count == 1U && fixture.states[0].events[0].timer_id == old_request.timer_id,
        "business consumes the old delivered event after timer exit");
    TEST_CHECK(consumeNextEvent(&fixture.queues[0], &fixture.states[0]) == ERR_QUEUE_EMPTY,
               "business drains the old lifecycle before IDs may be reused");
    dominoTimeModuleInitBefore();
    TEST_CHECK(dominoTimerModuleInit() == CODE_OK, "initialize the next timer lifecycle using the existing business queue");
    DominoTimer request = {.event_queue_ptr = &fixture.queues[0], .event_type = 99U};
    TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK && request.timer_id == old_request.timer_id, "new lifecycle may reuse the old command ID");
    TEST_CHECK(startDelivery() && consumeEvents(&fixture, 0U, 1U) && fixture.states[0].dispatch_count == 2U &&
                   fixture.states[0].events[1].timer_id == request.timer_id && fixture.states[0].events[1].event_type == 99U,
               "the drained business queue only delivers the new event when its ID is reused");
    TEST_CHECK(consumeNextEvent(&fixture.queues[0], &fixture.states[0]) == ERR_QUEUE_EMPTY,
               "all delivered events are consumed before the next timer exit");
    dominoTimerModuleExit();
    TEST_CHECK(!g_domino_timer.initialized_flag && fixture.queues[0].initialized && fixture.queues[0].items_ptr != nullptr &&
                   !dominoThreadQueueIsStopped(&fixture.queues[0], DOMINO_THREAD_QUEUE_PRODUCER) &&
                   !dominoThreadQueueIsStopped(&fixture.queues[0], DOMINO_THREAD_QUEUE_CONSUMER),
               "timer exit neither destroys nor stops the business queue");
    success_flag = true;

cleanup:
    destroyFixture(&fixture);
    return success_flag;
}

int main(void) {
    return testCancelBeforeDelivery() && testCancelInHeap() && testCancelQueuedDuringDelivery() && testFullQueueAbort() && testCancelClaimedEvent() &&
                   testConcurrentCancelAndConsume() && testBusinessQueueLifecycle()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
