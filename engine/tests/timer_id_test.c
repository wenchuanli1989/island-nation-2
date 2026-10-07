#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "domino_shared_common.h"
#include "time/entry.h"
#include "timer/internal.h"

#define TEST_WAIT_NS UINT64_C(2000000000)
#define TEST_QUEUE_COUNT 8U
#define TEST_PRODUCER_COUNT 4U
#define TEST_TASKS_PER_PRODUCER 128U
#define TEST_TASK_COUNT (TEST_PRODUCER_COUNT * TEST_TASKS_PER_PRODUCER)

#define TEST_CHECK(condition, message)                              \
    do {                                                            \
        if (!(condition)) {                                         \
            (void)fprintf(stderr, "%s: %s\n", __func__, (message)); \
            goto cleanup;                                           \
        }                                                           \
    } while (0)

typedef struct TimerProducer {
    _Atomic bool* start_flag_ptr;
    domino_timer_id_t timer_ids[TEST_TASKS_PER_PRODUCER];
    DOMINO_CODE result_codes[TEST_TASKS_PER_PRODUCER];
    DominoThreadQueue* event_queue_ptr;
    uint32_t task_count;
} TimerProducer;

typedef struct TimerScheduleCall {
    DominoTimer request;
    thrd_t thread;
    DOMINO_CODE code;
    _Atomic bool finished_flag;
    bool thread_started_flag;
} TimerScheduleCall;

static DominoThreadQueue g_test_event_queues[TEST_QUEUE_COUNT];

/** @brief 业务自行初始化事件队列，timer 只借用队列指针。 */
static bool initializeBusinessQueues(void) {
    for (uint32_t i = 0U; i < TEST_QUEUE_COUNT; ++i) {
        if (dominoThreadQueueInit(&g_test_event_queues[i], sizeof(DominoTimer), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                  DOMINO_ENGINE_QUEUE_MAX_CAPACITY) != CODE_OK) {
            return false;
        }
    }
    return true;
}

/** @brief 冻结期间计时线程只等待；业务队列初始化后直接启动计时线程。 */
static bool initializeFixture(void) {
    dominoTimeModuleInitBefore();
    return initializeBusinessQueues() && dominoTimerModuleInit() == CODE_OK;
}

/** @brief 先停止并 join timer，业务再销毁自己持有的队列。 */
static void exitFixture(void) {
    dominoTimerModuleExit();
    for (uint32_t i = 0U; i < TEST_QUEUE_COUNT; ++i) {
        dominoThreadQueueDestroy(&g_test_event_queues[i]);
    }
}

/** @brief 持请求锁检查堆数量，等待正常后台处理完成。 */
static bool waitForHeapCount(uint32_t expected_count) {
    uint64_t start_ns = dominoMonotonicTimeNs();
    while (dominoMonotonicTimeNs() - start_ns < TEST_WAIT_NS) {
        checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
        uint32_t count = g_domino_timer.heap.count;
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
        if (count == expected_count) {
            return true;
        }
        thrd_yield();
    }
    return false;
}

/** @brief 持锁检查冻结期间登记命令仍在队列中，计时堆保持为空。 */
static bool hasFrozenCommands(uint32_t expected_count) {
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    bool retained_flag = dominoTimeModuleIsFrozen() && g_domino_timer.command_queue.count == expected_count && g_domino_timer.heap.count == 0U;
    checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    return retained_flag;
}

static int runScheduleCall(void* argument_ptr) {
    TimerScheduleCall* call_ptr = argument_ptr;
    call_ptr->code = dominoTimerSchedule(&call_ptr->request);
    atomic_store_explicit(&call_ptr->finished_flag, true, memory_order_release);
    return 0;
}

/** @brief 在独立线程提交请求；调用方释放请求锁后再 join。 */
static bool startScheduleCall(TimerScheduleCall* call_ptr) {
    atomic_store_explicit(&call_ptr->finished_flag, false, memory_order_release);
    call_ptr->thread_started_flag = thrd_create(&call_ptr->thread, runScheduleCall, call_ptr) == thrd_success;
    return call_ptr->thread_started_flag;
}

static void joinScheduleCall(TimerScheduleCall* call_ptr) {
    if (call_ptr->thread_started_flag) {
        checkThreadResult(thrd_join(call_ptr->thread, nullptr));
        call_ptr->thread_started_flag = false;
    }
}

/** @brief 仅取命令队列锁观察入队状态，可在调用方持有请求锁时使用。 */
static bool waitForCommandCount(uint32_t expected_count) {
    uint64_t start_ns = dominoMonotonicTimeNs();
    while (dominoMonotonicTimeNs() - start_ns < TEST_WAIT_NS) {
        checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
        uint32_t command_count = g_domino_timer.command_queue.count;
        checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
        if (command_count == expected_count) {
            return true;
        }
        thrd_yield();
    }
    return false;
}

static int runProducer(void* argument_ptr) {
    TimerProducer* producer_ptr = argument_ptr;
    while (!atomic_load_explicit(producer_ptr->start_flag_ptr, memory_order_acquire)) {
        thrd_yield();
    }
    DominoTimer request = {.event_queue_ptr = producer_ptr->event_queue_ptr, .delay_ms = UINT32_MAX};
    for (uint32_t i = 0U; i < producer_ptr->task_count; ++i) {
        request.timer_id = UINT64_MAX;
        producer_ptr->result_codes[i] = dominoTimerSchedule(&request);
        producer_ptr->timer_ids[i] = request.timer_id;
    }
    return 0;
}

/** @brief 多业务队列并行提交，同时覆盖同一队列的两个调用线程。 */
static bool runProducers(TimerProducer producers[TEST_PRODUCER_COUNT], uint32_t task_count) {
    _Atomic bool start_flag = false;
    thrd_t threads[TEST_PRODUCER_COUNT];
    uint32_t thread_count = 0U;
    for (uint32_t i = 0U; i < TEST_PRODUCER_COUNT; ++i) {
        producers[i].start_flag_ptr = &start_flag;
        producers[i].event_queue_ptr = &g_test_event_queues[i % (TEST_PRODUCER_COUNT / 2U)];
        producers[i].task_count = task_count;
        if (thrd_create(&threads[i], runProducer, &producers[i]) != thrd_success) {
            break;
        }
        thread_count++;
    }
    atomic_store_explicit(&start_flag, true, memory_order_release);
    for (uint32_t i = 0U; i < thread_count; ++i) {
        checkThreadResult(thrd_join(threads[i], nullptr));
    }
    return thread_count == TEST_PRODUCER_COUNT;
}

static int compareTimerIds(const void* left_ptr, const void* right_ptr) {
    domino_timer_id_t left_id = *(const domino_timer_id_t*)left_ptr;
    domino_timer_id_t right_id = *(const domino_timer_id_t*)right_ptr;
    return (left_id > right_id) - (left_id < right_id);
}

/** @brief 无目标配置直接启动；提交和取消校验业务队列，Init/Exit 不管理其生命周期。 */
static bool testBusinessQueues(void) {
    bool success_flag = false;
    DominoThreadQueue uninitialized_queue = {0};
    DominoThreadQueue wrong_size_queue = {0};
    DominoTimer request = {.timer_id = UINT64_MAX, .delay_ms = UINT32_MAX};
    dominoTimeModuleInitBefore();
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_NOT_INITIALIZED && request.timer_id == 0U,
               "an uninitialized timer does not accept a request or publish an ID");
    TEST_CHECK(dominoTimerModuleInit() == CODE_OK, "timer can start without any target configuration or business queue");
    TEST_CHECK(dominoTimerModuleInit() == ERR_ALREADY_INITIALIZED, "an active module rejects reinitialization");
    TEST_CHECK(dominoTimerSchedule(nullptr) == ERR_NULL_POINTER, "schedule requires a writable request");
    request.timer_id = UINT64_MAX;
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_NULL_POINTER && request.timer_id == 0U, "a schedule requires a business event queue");
    TEST_CHECK(dominoTimerCancel(1U, nullptr) == ERR_NULL_POINTER, "cancellation requires a business event queue");
    request.event_queue_ptr = &uninitialized_queue;
    request.timer_id = UINT64_MAX;
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_NOT_INITIALIZED && request.timer_id == 0U,
               "an uninitialized business queue cannot receive timers");
    TEST_CHECK(dominoTimerCancel(1U, &uninitialized_queue) == ERR_NOT_INITIALIZED, "cancellation rejects an uninitialized business queue");
    TEST_CHECK(dominoThreadQueueInit(&wrong_size_queue, sizeof(uint64_t), 1U, 1U) == CODE_OK, "initialize a queue with another message type");
    request.event_queue_ptr = &wrong_size_queue;
    request.timer_id = UINT64_MAX;
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_INVALID_PARAM && request.timer_id == 0U,
               "schedule rejects a business queue with a different element size");
    TEST_CHECK(dominoTimerCancel(1U, &wrong_size_queue) == ERR_INVALID_PARAM, "cancellation rejects a business queue with a different element size");
    TEST_CHECK(g_domino_timer.command_queue.next_id == 0U, "business queue validation consumes no command IDs");
    TEST_CHECK(initializeBusinessQueues(), "business initializes its own timer event queues after the timer worker starts");
    for (uint32_t i = 0U; i < TEST_QUEUE_COUNT; ++i) {
        request.event_queue_ptr = &g_test_event_queues[i];
        TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK && request.timer_id == i + 1U,
                   "every initialized business queue can receive timers without registration");
    }
    TEST_CHECK(hasFrozenCommands(TEST_QUEUE_COUNT), "frozen timer retains every registration in the command queue without building the heap");
    dominoTimeModuleInitAfter();
    dominoTimerNotifyTimeChanged();
    TEST_CHECK(waitForHeapCount(TEST_QUEUE_COUNT), "thawed worker moves future registrations into the global heap");
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    const DominoTimer* event_ptr = dominoTimerHeapPeek(&g_domino_timer.heap);
    bool correct_route_flag = event_ptr != nullptr && event_ptr->event_queue_ptr == &g_test_event_queues[0];
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    TEST_CHECK(correct_route_flag, "the heap record retains its caller-provided queue pointer");
    DominoTimer business_event = {.event_queue_ptr = &g_test_event_queues[0], .timer_id = 99U, .event_type = 7U};
    TEST_CHECK(dominoThreadQueueProduce(&g_test_event_queues[0], &business_event, false) == CODE_OK,
               "business can produce an event in its queue before timer exit");
    dominoTimerModuleExit();
    dominoTimerModuleExit();
    TEST_CHECK(g_test_event_queues[0].initialized && g_test_event_queues[0].count == 1U && g_test_event_queues[0].next_id == business_event.id &&
                   !g_test_event_queues[0].producer_stopped && !g_test_event_queues[0].consumer_stopped,
               "timer exit does not stop, destroy, clear, or reset the business queue");
    DominoTimer consumed_event;
    TEST_CHECK(dominoThreadQueueConsume(&g_test_event_queues[0], &consumed_event, false) == CODE_OK && consumed_event.id == business_event.id &&
                   consumed_event.timer_id == 99U && consumed_event.event_type == 7U && consumed_event.event_queue_ptr == &g_test_event_queues[0],
               "business directly consumes its preserved event after timer exit");
    dominoTimeModuleInitBefore();
    TEST_CHECK(dominoTimerModuleInit() == CODE_OK,
               "timer can be reinitialized with frozen time after business drains the previous lifecycle's events");
    TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK && request.timer_id == 1U,
               "reinitialization resets only timer command IDs and still accepts the business queue");
    success_flag = true;

cleanup:
    exitFixture();
    dominoThreadQueueDestroy(&wrong_size_queue);
    return success_flag;
}

static bool testConcurrentIds(void) {
    bool success_flag = false;
    bool request_locked = false;
    domino_timer_id_t sorted_ids[TEST_TASK_COUNT];
    DominoThreadQueue* expected_queues[TEST_TASK_COUNT] = {0};
    uint32_t queue_event_counts[TEST_QUEUE_COUNT] = {0};
    uint32_t expected_queue_counts[TEST_QUEUE_COUNT] = {0};
    TimerProducer producers[TEST_PRODUCER_COUNT] = {0};
    TEST_CHECK(initializeFixture(), "initialize timer with frozen accumulated runtime");
    DominoTimer invalid_request = {.timer_id = UINT64_MAX};
    TEST_CHECK(dominoTimerSchedule(&invalid_request) == ERR_NULL_POINTER && invalid_request.timer_id == 0U,
               "missing business queue is rejected without publishing an ID");
    TEST_CHECK(g_domino_timer.command_queue.next_id == 0U, "queue validation precedes ID allocation");
    TEST_CHECK(runProducers(producers, TEST_TASKS_PER_PRODUCER), "run concurrent producers");
    for (uint32_t i = 0U; i < TEST_PRODUCER_COUNT; ++i) {
        expected_queue_counts[producers[i].event_queue_ptr - g_test_event_queues] += TEST_TASKS_PER_PRODUCER;
        for (uint32_t j = 0U; j < TEST_TASKS_PER_PRODUCER; ++j) {
            TEST_CHECK(producers[i].result_codes[j] == CODE_OK && producers[i].timer_ids[j] != 0U, "concurrent scheduling succeeds with nonzero IDs");
            TEST_CHECK(producers[i].timer_ids[j] <= TEST_TASK_COUNT, "new lifecycle allocates IDs within the submitted batch");
            sorted_ids[i * TEST_TASKS_PER_PRODUCER + j] = producers[i].timer_ids[j];
            expected_queues[producers[i].timer_ids[j] - 1U] = producers[i].event_queue_ptr;
        }
    }
    qsort(sorted_ids, TEST_TASK_COUNT, sizeof(*sorted_ids), compareTimerIds);
    for (uint32_t i = 1U; i < TEST_TASK_COUNT; ++i) {
        TEST_CHECK(sorted_ids[i] != sorted_ids[i - 1U], "concurrent schedules receive distinct IDs");
    }
    TEST_CHECK(hasFrozenCommands(TEST_TASK_COUNT), "frozen worker leaves the complete concurrent batch queued without heap operations");
    dominoTimeModuleInitAfter();
    dominoTimerNotifyTimeChanged();
    TEST_CHECK(waitForHeapCount(TEST_TASK_COUNT), "thawed worker registers the complete accepted batch");
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    request_locked = true;
    DominoTimerHeap* heap_ptr = &g_domino_timer.heap;
    TEST_CHECK(heap_ptr->count == TEST_TASK_COUNT, "all business queues retain accepted timers in the same heap");
    domino_timer_id_t previous_id = 0U;
    while (heap_ptr->count != 0U) {
        const DominoTimer* event_ptr = dominoTimerHeapPeek(heap_ptr);
        TEST_CHECK(event_ptr->due_ms == UINT32_MAX && event_ptr->timer_id > previous_id,
                   "equal deadlines retain global registration order across business queues");
        TEST_CHECK(bsearch(&event_ptr->timer_id, sorted_ids, TEST_TASK_COUNT, sizeof(*sorted_ids), compareTimerIds) != nullptr,
                   "each stored timer retains its returned command ID");
        TEST_CHECK(event_ptr->event_queue_ptr == expected_queues[event_ptr->timer_id - 1U], "each timer retains its submitting business queue");
        queue_event_counts[event_ptr->event_queue_ptr - g_test_event_queues]++;
        previous_id = event_ptr->timer_id;
        dominoTimerHeapPop(heap_ptr);
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    request_locked = false;
    for (uint32_t i = 0U; i < TEST_QUEUE_COUNT; ++i) {
        TEST_CHECK(queue_event_counts[i] == expected_queue_counts[i], "global heap retains every business queue's complete accepted batch");
    }
    success_flag = true;

cleanup:
    if (request_locked) {
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    }
    exitFixture();
    return success_flag;
}

/** @brief 并发争抢最后一个可用 ID；耗尽后不回绕、不返回旧 ID。 */
static bool testIdExhaustion(void) {
    bool success_flag = false;
    uint32_t successful_count = 0U;
    DominoThreadQueue* successful_queue_ptr = nullptr;
    TimerProducer producers[TEST_PRODUCER_COUNT] = {0};
    TEST_CHECK(initializeFixture(), "initialize timer with frozen accumulated runtime");
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    g_domino_timer.command_queue.next_id = UINT64_MAX - 1U;
    checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    TEST_CHECK(runProducers(producers, 1U), "race producers for final ID");
    for (uint32_t i = 0U; i < TEST_PRODUCER_COUNT; ++i) {
        if (producers[i].result_codes[0] == CODE_OK) {
            successful_count++;
            successful_queue_ptr = producers[i].event_queue_ptr;
            TEST_CHECK(producers[i].timer_ids[0] == UINT64_MAX, "last ID is usable");
        } else {
            TEST_CHECK(producers[i].result_codes[0] == ERR_OUT_OF_RANGE && producers[i].timer_ids[0] == 0U,
                       "exhausted allocation fails without publishing an ID");
        }
    }
    TEST_CHECK(successful_count == 1U, "exactly one producer receives the final ID");
    DominoTimer request = {.timer_id = UINT64_MAX, .event_queue_ptr = &g_test_event_queues[0]};
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_OUT_OF_RANGE && request.timer_id == 0U, "exhausted command queue does not permit ID reuse");
    TEST_CHECK(g_domino_timer.command_queue.next_id == UINT64_MAX, "ID counter stays saturated");
    TEST_CHECK(hasFrozenCommands(1U), "frozen worker retains the final-ID request in the command queue");
    dominoTimeModuleInitAfter();
    dominoTimerNotifyTimeChanged();
    TEST_CHECK(waitForHeapCount(1U), "thawed worker registers the final request without allocating another ID");
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    const DominoTimerHeap* heap_ptr = &g_domino_timer.heap;
    const DominoTimer* event_ptr = dominoTimerHeapPeek(heap_ptr);
    bool correct_identity_flag =
        heap_ptr->count == 1U && event_ptr != nullptr && event_ptr->timer_id == UINT64_MAX && event_ptr->event_queue_ptr == successful_queue_ptr;
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    TEST_CHECK(correct_identity_flag, "last ID identifies the accepted timer and its business queue in the global heap");
    success_flag = true;

cleanup:
    exitFixture();
    return success_flag;
}

/** @brief 非零冻结时间下处理零延迟、正延迟和溢出边界，成功回写消息 ID、timer_id 和绝对到期时间。 */
static bool testDelayBounds(void) {
    bool success_flag = false;
    bool command_locked = false;
    domino_timer_id_t timer_ids[3];
    TEST_CHECK(initializeFixture(), "initialize timer with frozen time");
    dominoSetRuntimeDateTimeBase(UINT64_C(2000000000));
    uint64_t now_ms = dominoTimeGetRuntimeMs();
    TEST_CHECK(now_ms == UINT64_C(2000), "a frozen two-second baseline equals 2000 accumulated runtime milliseconds");
    uint32_t overflowing_delay_ms = UINT32_MAX - (uint32_t)now_ms + 1U;
    DominoTimer request = {
        .id = 77U, .timer_id = UINT64_MAX, .event_queue_ptr = &g_test_event_queues[0], .due_ms = 99U, .delay_ms = overflowing_delay_ms};
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_OUT_OF_RANGE && request.timer_id == 0U && request.id == 77U && request.due_ms == 99U,
               "overflowing delay clears timer_id while preserving the caller's message ID and deadline");
    TEST_CHECK(request.delay_ms == overflowing_delay_ms, "rejection preserves the requested relative delay");
    uint64_t out_of_range_base_ns = ((uint64_t)UINT32_MAX + 1U) * UINT64_C(1000000);
    dominoSetRuntimeDateTimeBase(out_of_range_base_ns);
    TEST_CHECK(dominoTimeGetRuntimeMs() > UINT32_MAX, "frozen accumulated runtime exceeds the 32-bit deadline range");
    request.delay_ms = 0U;
    request.timer_id = UINT64_MAX;
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_OUT_OF_RANGE && request.timer_id == 0U && request.id == 77U && request.due_ms == 99U,
               "out-of-range current time rejects even a zero delay without changing the message ID or deadline");
    dominoSetRuntimeDateTimeBase(UINT64_C(2000000000));
    const uint32_t delays_ms[] = {0U, 350U, UINT32_MAX - (uint32_t)now_ms};
    for (uint32_t i = 0U; i < 3U; ++i) {
        request.delay_ms = delays_ms[i];
        TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK && request.timer_id == i + 1U && request.id == request.timer_id &&
                       request.due_ms == now_ms + delays_ms[i],
                   "accepted delays overwrite message ID and deadline and receive consecutive timer IDs after overflow rejection");
        timer_ids[i] = request.timer_id;
        TEST_CHECK(request.delay_ms == delays_ms[i], "scheduling preserves the requested relative delay");
    }
    TEST_CHECK(hasFrozenCommands(3U), "frozen worker retains zero, positive, and maximum deadlines as commands without heap operations");
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    command_locked = true;
    DominoThreadQueue* queue_ptr = &g_domino_timer.command_queue;
    TEST_CHECK(queue_ptr->count == 3U && queue_ptr->next_id == 3U, "overflow creates no command and consumes no command ID");
    const DominoTimer* commands_ptr = queue_ptr->items_ptr;
    for (uint32_t i = 0U; i < 3U; ++i) {
        const DominoTimer* command_ptr = &commands_ptr[(queue_ptr->head + i) % queue_ptr->capacity];
        TEST_CHECK(command_ptr->id == timer_ids[i] && command_ptr->timer_id == 0U && command_ptr->delay_ms == delays_ms[i] &&
                       command_ptr->due_ms == now_ms + delays_ms[i] && command_ptr->event_queue_ptr == &g_test_event_queues[0],
                   "queued command stores its registration ID, original delay, business queue, and absolute deadline");
    }
    success_flag = true;

cleanup:
    if (command_locked) {
        checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    }
    exitFixture();
    return success_flag;
}

/** @brief 成功登记先入队再等待请求锁通知；拒绝登记不通知，命令环绕和 ID 分配保持正确。 */
static bool testScheduleNotificationWaits(void) {
    bool success_flag = false;
    bool request_locked = false;
    TimerScheduleCall calls[3] = {0};
    DominoTimer request = {
        .id = 77U, .timer_id = UINT64_MAX, .event_queue_ptr = &g_test_event_queues[0], .due_ms = 99U, .delay_ms = UINT32_MAX, .cancelled_flag = true};
    domino_timer_id_t active_ids[2];
    TEST_CHECK(initializeFixture(), "initialize running timer");
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    request_locked = true;
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    // 现有分配足够容纳两个元素，只收紧逻辑容量；worker 被请求锁挡住，不能转移命令。
    g_domino_timer.command_queue.capacity = 2U;
    g_domino_timer.command_queue.max_capacity = 2U;
    checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_INVALID_PARAM && request.timer_id == 0U && request.id == 77U && request.due_ms == 99U &&
                   request.cancelled_flag,
               "cancelled request clears timer_id while preserving the caller's message ID, deadline, and cancellation flag");
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    bool cancelled_skipped_flag = g_domino_timer.command_queue.count == 0U && g_domino_timer.command_queue.next_id == 0U;
    checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    TEST_CHECK(cancelled_skipped_flag, "cancelled request does not occupy the command queue or consume a message ID");
    request.cancelled_flag = false;
    domino_timer_id_t removed_id = 0U;
    for (uint32_t i = 0U; i < 3U; ++i) {
        calls[i].request = request;
        TEST_CHECK(startScheduleCall(&calls[i]), "start a successful submission while the request mutex is held");
        TEST_CHECK(waitForCommandCount(i == 0U ? 1U : i), "accepted request reaches the command queue before notification completes");
        TEST_CHECK(!atomic_load_explicit(&calls[i].finished_flag, memory_order_acquire),
                   "successful Schedule waits for the request mutex notification");
        if (i == 0U) {
            DominoTimer command;
            TEST_CHECK(dominoThreadQueueConsume(&g_domino_timer.command_queue, &command, false) == CODE_OK,
                       "advance the command ring before filling it");
            TEST_CHECK(command.id != 0U && !command.cancelled_flag, "Schedule preserves the uncancelled request flag");
            removed_id = command.id;
        } else {
            checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
            active_ids[i - 1U] = g_domino_timer.command_queue.next_id;
            checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
        }
    }
    request.timer_id = UINT64_MAX;
    TEST_CHECK(dominoTimerSchedule(&request) == ERR_QUEUE_FULL && request.timer_id == 0U && request.id == 77U && request.due_ms == UINT32_MAX,
               "full queue clears timer_id and preserves the message ID while retaining the deadline calculated before Produce");
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    const DominoTimer* commands_ptr = g_domino_timer.command_queue.items_ptr;
    bool queue_correct = g_domino_timer.command_queue.count == 2U && g_domino_timer.command_queue.head == 1U &&
                         g_domino_timer.command_queue.tail == 1U && g_domino_timer.command_queue.next_id == active_ids[1] &&
                         commands_ptr[1].id == active_ids[0] && commands_ptr[0].id == active_ids[1] && !commands_ptr[1].cancelled_flag &&
                         !commands_ptr[0].cancelled_flag;
    checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    TEST_CHECK(queue_correct && g_domino_timer.heap.count == 0U,
               "ring wrap and failed submission preserve pending commands without consuming an extra ID");
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    request_locked = false;
    for (uint32_t i = 0U; i < 3U; ++i) {
        joinScheduleCall(&calls[i]);
        TEST_CHECK(calls[i].code == CODE_OK && calls[i].request.timer_id == (i == 0U ? removed_id : active_ids[i - 1U]) &&
                       calls[i].request.id == calls[i].request.timer_id && calls[i].request.due_ms == UINT32_MAX,
                   "accepted submission returns its queued message ID, timer ID, and deadline after the request mutex is released");
    }
    TEST_CHECK(hasFrozenCommands(2U), "releasing the notification mutex leaves frozen commands queued and the heap empty");
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    DominoTimer first_command = commands_ptr[g_domino_timer.command_queue.head];
    DominoTimer second_command = commands_ptr[(g_domino_timer.command_queue.head + 1U) % g_domino_timer.command_queue.capacity];
    checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    TEST_CHECK(first_command.id == active_ids[0] && second_command.id == active_ids[1] && first_command.timer_id == 0U &&
                   second_command.timer_id == 0U && !first_command.cancelled_flag && !second_command.cancelled_flag,
               "frozen command ring preserves FIFO order after notifications complete");
    success_flag = true;

cleanup:
    if (request_locked) {
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    }
    for (uint32_t i = 0U; i < 3U; ++i) {
        joinScheduleCall(&calls[i]);
    }
    exitFixture();
    return success_flag;
}

/** @brief 请求入队后仍等待通知时推进冻结时间；命令记录登记时的到期时间，冻结期间不入堆。 */
static bool testDeadlineIncludesQueueDelay(void) {
    bool success_flag = false;
    bool request_locked = false;
    TimerScheduleCall call = {
        .request = {.id = 77U, .timer_id = 88U, .event_queue_ptr = &g_test_event_queues[0], .due_ms = UINT32_MAX, .delay_ms = 75U, .event_type = 3U}};
    TEST_CHECK(initializeFixture(), "initialize timer with frozen time");
    dominoSetRuntimeDateTimeBase(UINT64_C(1000000000));
    uint64_t original_ms = dominoTimeGetRuntimeMs();
    TEST_CHECK(original_ms == UINT64_C(1000), "a frozen one-second baseline equals 1000 accumulated runtime milliseconds");
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    request_locked = true;
    TEST_CHECK(startScheduleCall(&call) && waitForCommandCount(1U), "request is queued before accumulated runtime advances");
    TEST_CHECK(!atomic_load_explicit(&call.finished_flag, memory_order_acquire), "queued Schedule still waits for its notification mutex");
    dominoSetRuntimeDateTimeBase(UINT64_C(2000000000));
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    request_locked = false;
    joinScheduleCall(&call);
    TEST_CHECK(call.code == CODE_OK && call.request.timer_id != 0U, "queued Schedule returns successfully after notification can acquire the mutex");
    TEST_CHECK(hasFrozenCommands(1U), "past-due registration remains queued while frozen without heap insertion or delivery");
    checkThreadResult(mtx_lock(&g_domino_timer.command_queue.mutex));
    const DominoTimer* commands_ptr = g_domino_timer.command_queue.items_ptr;
    DominoTimer command = commands_ptr[g_domino_timer.command_queue.head];
    checkThreadResult(mtx_unlock(&g_domino_timer.command_queue.mutex));
    TEST_CHECK(command.due_ms == original_ms + 75U && command.due_ms < dominoTimeGetRuntimeMs(),
               "queued deadline uses Schedule time rather than the later notification completion time");
    TEST_CHECK(call.request.id == call.request.timer_id && call.request.id != 77U && call.request.timer_id != 88U &&
                   call.request.event_queue_ptr == &g_test_event_queues[0] && call.request.due_ms == original_ms + 75U &&
                   call.request.due_ms == command.due_ms && call.request.delay_ms == 75U && command.id == call.request.timer_id &&
                   command.timer_id == 0U && command.delay_ms == 75U && command.event_queue_ptr == &g_test_event_queues[0] &&
                   call.request.event_type == 3U && !call.request.cancelled_flag && command.event_type == 3U && !command.cancelled_flag,
               "successful registration overwrites message ID, timer ID, and deadline while preserving business payload in caller and command");
    success_flag = true;

cleanup:
    if (request_locked) {
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    }
    joinScheduleCall(&call);
    exitFixture();
    return success_flag;
}

/** @brief 混合到期时间触发堆扩容；同刻按 timer_id 保持登记顺序。 */
static bool testHeapOrder(void) {
    bool success_flag = false;
    DominoTimerHeap heap = {0};
    uint32_t previous_due_ms = 0U;
    domino_timer_id_t previous_id = 0U;
    bool seen_flags[TEST_TASK_COUNT] = {false};
    TEST_CHECK(dominoTimerHeapPeek(&heap) == nullptr, "zero initialized heap is empty");
    for (uint32_t i = 0U; i < TEST_TASK_COUNT; ++i) {
        DominoTimer event = {
            .delay_ms = i + 1U,
            .event_queue_ptr = &g_test_event_queues[i % TEST_QUEUE_COUNT],
            .due_ms = (i * 37U) % 17U,
            .timer_id = TEST_TASK_COUNT - i,
        };
        TEST_CHECK(dominoTimerHeapPush(&heap, &event) == CODE_OK, "push mixed deadlines through heap growth");
    }
    TEST_CHECK(heap.count == TEST_TASK_COUNT && heap.capacity >= TEST_TASK_COUNT, "heap grows without losing events");
    for (uint32_t i = 0U; i < TEST_TASK_COUNT; ++i) {
        const DominoTimer* event_ptr = dominoTimerHeapPeek(&heap);
        TEST_CHECK(event_ptr != nullptr, "each accepted event can be popped");
        uint32_t due_ms = event_ptr->due_ms;
        TEST_CHECK(i == 0U || due_ms > previous_due_ms || (due_ms == previous_due_ms && event_ptr->timer_id > previous_id),
                   "events sort by deadline and then timer ID");
        TEST_CHECK(event_ptr->timer_id >= 1U && event_ptr->timer_id <= TEST_TASK_COUNT && !seen_flags[event_ptr->timer_id - 1U],
                   "each timer ID appears exactly once after heap growth");
        seen_flags[event_ptr->timer_id - 1U] = true;
        TEST_CHECK(event_ptr->delay_ms == TEST_TASK_COUNT - event_ptr->timer_id + 1U &&
                       event_ptr->event_queue_ptr == &g_test_event_queues[(TEST_TASK_COUNT - event_ptr->timer_id) % TEST_QUEUE_COUNT],
                   "global heap preserves each event payload and business queue");
        previous_due_ms = due_ms;
        previous_id = event_ptr->timer_id;
        dominoTimerHeapPop(&heap);
    }
    TEST_CHECK(heap.count == 0U && dominoTimerHeapPeek(&heap) == nullptr, "popping all events leaves an empty heap");
    success_flag = true;

cleanup:
    dominoTimerHeapExit(&heap);
    return success_flag;
}

int main(void) {
    return testBusinessQueues() && testScheduleNotificationWaits() && testConcurrentIds() && testIdExhaustion() && testDelayBounds() &&
                   testDeadlineIncludesQueueDelay() && testHeapOrder()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
