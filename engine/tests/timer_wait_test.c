#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "domino_shared_common.h"
#include "time/entry.h"
#include "timer/internal.h"

#define TEST_WAIT_NS UINT64_C(2000000000)
#define TEST_SHORT_DELAY_MS UINT32_C(50)
#define TEST_LONG_DELAY_MS UINT32_C(60000)

#define TEST_CHECK(condition, message)                              \
    do {                                                            \
        if (!(condition)) {                                         \
            (void)fprintf(stderr, "%s: %s\n", __func__, (message)); \
            goto cleanup;                                           \
        }                                                           \
    } while (0)

typedef struct TimerDispatchState {
    thrd_t dispatch_thread;
    domino_timer_id_t timer_id;
    uint64_t event_id;
    uint64_t dispatch_ms;
    uint32_t due_ms;
    uint32_t delay_ms;
    uint32_t dispatch_count;
    bool correct_thread;
} TimerDispatchState;

static void recordTimerEvent(const DominoTimer* event_ptr, TimerDispatchState* state_ptr) {
    state_ptr->timer_id = event_ptr->timer_id;
    state_ptr->event_id = event_ptr->id;
    state_ptr->dispatch_ms = dominoTimeGetRuntimeMs();
    state_ptr->due_ms = event_ptr->due_ms;
    state_ptr->delay_ms = event_ptr->delay_ms;
    state_ptr->dispatch_count++;
    state_ptr->correct_thread = thrd_equal(thrd_current(), state_ptr->dispatch_thread) != 0;
}

/** @brief 业务消费者自行取出事件并过滤取消标志。 */
static DOMINO_CODE consumeTimerEvent(DominoThreadQueue* queue_ptr, TimerDispatchState* state_ptr) {
    DominoTimer event;
    DOMINO_CODE code = dominoThreadQueueConsume(queue_ptr, &event, false);
    if (code == CODE_OK && !event.cancelled_flag) {
        recordTimerEvent(&event, state_ptr);
    }
    return code;
}

/** @brief 空队列仅轮询消费端，不提交命令或唤醒计时线程，以验证其自身的超时唤醒。 */
static bool dispatchWithinDeadline(DominoThreadQueue* queue_ptr, TimerDispatchState* state_ptr) {
    uint64_t start_ns = dominoMonotonicTimeNs();
    while (dominoMonotonicTimeNs() - start_ns < TEST_WAIT_NS) {
        DOMINO_CODE code = consumeTimerEvent(queue_ptr, state_ptr);
        if (code == CODE_OK) {
            return true;
        }
        if (code != ERR_QUEUE_EMPTY) {
            (void)fprintf(stderr, "%s: dispatch failed (%d)\n", __func__, code);
            return false;
        }
        thrd_yield();
    }
    (void)fprintf(stderr, "%s: timer did not arrive within two seconds\n", __func__);
    return false;
}

static bool waitForQueuedEvent(DominoThreadQueue* queue_ptr) {
    uint64_t start_ns = dominoMonotonicTimeNs();
    while (dominoMonotonicTimeNs() - start_ns < TEST_WAIT_NS) {
        checkThreadResult(mtx_lock(&queue_ptr->mutex));
        bool queued_flag = queue_ptr->count == 1U;
        checkThreadResult(mtx_unlock(&queue_ptr->mutex));
        if (queued_flag) {
            return true;
        }
        thrd_yield();
    }
    return false;
}

/** @brief 验证自主到期唤醒、较早任务唤醒、冻结时提交和消费，以及等待期间退出。 */
static bool testTimerWaits(void) {
    bool success = false;
    TimerDispatchState state = {.dispatch_thread = thrd_current()};
    DominoThreadQueue event_queue = {0};
    DominoThreadQueue long_event_queue = {0};
    domino_timer_id_t queued_id;
    domino_timer_id_t frozen_id;
    domino_timer_id_t exit_queued_id;
    dominoTimeModuleInitBefore();
    dominoTimeModuleInitAfter();
    TEST_CHECK(
        dominoThreadQueueInit(&event_queue, sizeof(DominoTimer), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY, DOMINO_ENGINE_QUEUE_MAX_CAPACITY) == CODE_OK,
        "initialize the business event queue");
    TEST_CHECK(dominoThreadQueueInit(&long_event_queue, sizeof(DominoTimer), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                     DOMINO_ENGINE_QUEUE_MAX_CAPACITY) == CODE_OK,
               "initialize the other business event queue");
    TEST_CHECK(dominoTimerModuleInit() == CODE_OK, "initialize timer");

    DominoTimer long_request = {.event_queue_ptr = &long_event_queue, .delay_ms = TEST_LONG_DELAY_MS};
    TEST_CHECK(dominoTimerSchedule(&long_request) == CODE_OK, "register long future timer");
    uint64_t short_due_lower_bound = dominoTimeGetRuntimeMs() + TEST_SHORT_DELAY_MS;
    DominoTimer short_request = {.event_queue_ptr = &event_queue, .delay_ms = TEST_SHORT_DELAY_MS};
    TEST_CHECK(dominoTimerSchedule(&short_request) == CODE_OK, "register short future timer");
    TEST_CHECK(dispatchWithinDeadline(&event_queue, &state),
               "another queue's short timer replaces the global long wait without intervening commands");
    TEST_CHECK(state.dispatch_count == 1U && state.timer_id == short_request.timer_id && state.due_ms >= short_due_lower_bound &&
                   state.dispatch_ms >= state.due_ms && state.correct_thread,
               "short timer does not fire early and business handling runs on the consumer thread");
    TEST_CHECK(short_request.delay_ms == TEST_SHORT_DELAY_MS && state.delay_ms == TEST_SHORT_DELAY_MS,
               "scheduling and delivery preserve the original positive delay");
    TEST_CHECK(state.event_id == 1U && state.event_id != short_request.timer_id, "event queue ID does not overwrite the originating timer ID");

    uint64_t earlier_due_lower_bound = dominoTimeGetRuntimeMs() + TEST_SHORT_DELAY_MS;
    DominoTimer earlier_request = {.event_queue_ptr = &event_queue, .delay_ms = TEST_SHORT_DELAY_MS};
    TEST_CHECK(dominoTimerSchedule(&earlier_request) == CODE_OK, "insert an earlier deadline while a long future timer remains");
    TEST_CHECK(dispatchWithinDeadline(&event_queue, &state), "new earlier deadline replaces the long future wait");
    TEST_CHECK(state.dispatch_count == 2U && state.timer_id == earlier_request.timer_id && state.due_ms >= earlier_due_lower_bound &&
                   state.dispatch_ms >= state.due_ms && state.correct_thread,
               "earlier timer is delivered on time and through the dispatch thread");
    TEST_CHECK(state.event_id == 2U, "event IDs increment independently of command IDs");

    DominoTimer zero_request = {.event_queue_ptr = &event_queue, .delay_ms = 0U};
    TEST_CHECK(dominoTimerSchedule(&zero_request) == CODE_OK, "register an event before freezing runtime");
    queued_id = zero_request.timer_id;
    TEST_CHECK(waitForQueuedEvent(&event_queue), "queue an event before freezing runtime");
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    dominoTimeModuleExit();
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    dominoTimerNotifyTimeChanged();
    TEST_CHECK(dominoTimeModuleIsFrozen(), "freeze runtime without a timer pause state");
    TEST_CHECK(dominoTimerSchedule(&zero_request) == CODE_OK && zero_request.timer_id != 0U, "frozen runtime still permits registration");
    frozen_id = zero_request.timer_id;
    TEST_CHECK(
        consumeTimerEvent(&event_queue, &state) == CODE_OK && state.dispatch_count == 3U && state.timer_id == queued_id && state.correct_thread,
        "frozen runtime still permits business consumption of an event that is already queued");
    TEST_CHECK(consumeTimerEvent(&event_queue, &state) == ERR_QUEUE_EMPTY, "newly registered event awaits a later heap pass");
    dominoTimeModuleInitAfter();
    dominoTimerNotifyTimeChanged();
    TEST_CHECK(dispatchWithinDeadline(&event_queue, &state), "time change notification wakes the worker after thaw without another submission");
    TEST_CHECK(state.dispatch_count == 4U && state.timer_id == frozen_id && state.correct_thread && state.delay_ms == 0U &&
                   state.dispatch_ms >= state.due_ms,
               "thaw delivers the accepted zero-delay event through the consumer thread");

    TEST_CHECK(dominoTimerSchedule(&zero_request) == CODE_OK, "register an event for business consumption after timer exit");
    exit_queued_id = zero_request.timer_id;
    TEST_CHECK(waitForQueuedEvent(&event_queue), "queue an event for business consumption after timer exit");
    uint64_t exit_start_ns = dominoMonotonicTimeNs();
    dominoTimerModuleExit();
    TEST_CHECK(dominoMonotonicTimeNs() - exit_start_ns < TEST_WAIT_NS, "exit joins the worker without waiting for the future deadline");
    TEST_CHECK(state.timer_id != long_request.timer_id && !g_domino_timer.initialized_flag, "exit discards the long timer and releases the module");
    DominoTimer exit_request_before = zero_request;
    TEST_CHECK(dominoTimerSchedule(&zero_request) == ERR_NOT_INITIALIZED &&
                   dominoTimerCancel(long_request.timer_id, &long_event_queue) == ERR_NOT_INITIALIZED,
               "exit removes scheduling and cancellation services");
    TEST_CHECK(zero_request.timer_id == 0U && zero_request.id == exit_request_before.id &&
                   zero_request.event_queue_ptr == exit_request_before.event_queue_ptr && zero_request.due_ms == exit_request_before.due_ms &&
                   zero_request.delay_ms == exit_request_before.delay_ms && zero_request.event_type == exit_request_before.event_type &&
                   zero_request.cancelled_flag == exit_request_before.cancelled_flag,
               "failed registration clears the timer ID and preserves all other request fields");
    TEST_CHECK(
        consumeTimerEvent(&event_queue, &state) == CODE_OK && state.dispatch_count == 5U && state.timer_id == exit_queued_id && state.correct_thread,
        "business queue remains consumable after timer exit");
    TEST_CHECK(consumeTimerEvent(&event_queue, &state) == ERR_QUEUE_EMPTY && consumeTimerEvent(&long_event_queue, &state) == ERR_QUEUE_EMPTY,
               "timer exit preserves both business queues and discards the pending long timer");
    success = true;

cleanup:
    dominoTimerModuleExit();
    dominoTimeModuleExit();
    dominoThreadQueueDestroy(&long_event_queue);
    dominoThreadQueueDestroy(&event_queue);
    return success;
}

int main(void) {
    return testTimerWaits() ? EXIT_SUCCESS : EXIT_FAILURE;
}
