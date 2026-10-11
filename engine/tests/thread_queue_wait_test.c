#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "queue/thread_queue.h"
#include "thread_message_test_helpers.h"
#include "time/entry.h"
#include "timer/entry.h"

#define TEST_CHECK(condition, message)                              \
    do {                                                            \
        if (!(condition)) {                                         \
            (void)fprintf(stderr, "%s: %s\n", __func__, (message)); \
            goto cleanup;                                           \
        }                                                           \
    } while (0)

typedef struct TestMessage {
    uint64_t id;
    uint32_t value;
} TestMessage;

typedef struct WaitOperation {
    DominoThreadQueue* queue_ptr;
    uint64_t observed_epoch;
    struct timespec deadline_utc;
} WaitOperation;

typedef enum TestControl {
    TEST_RUNNING,
    TEST_PAUSED,
    TEST_STOPPING,
} TestControl;

typedef struct ControlledWorker {
    DominoThreadQueue* queue_ptr;
    _Atomic TestControl requested_state;
    _Atomic TestControl observed_state;
    _Atomic uint32_t consumed_count;
} ControlledWorker;

static bool getDeadline(struct timespec* deadline_ptr) {
    if (timespec_get(deadline_ptr, TIME_UTC) != TIME_UTC) {
        return false;
    }
    deadline_ptr->tv_sec += 5;
    return true;
}

static bool beforeDeadline(const struct timespec* deadline_ptr) {
    struct timespec now;
    return timespec_get(&now, TIME_UTC) == TIME_UTC &&
           (now.tv_sec < deadline_ptr->tv_sec || (now.tv_sec == deadline_ptr->tv_sec && now.tv_nsec < deadline_ptr->tv_nsec));
}

/** @brief 检查实际条件等待登记，避免用固定睡眠猜测线程已进入 Wait。 */
static bool waitForWaiters(DominoThreadQueue* queue_ptr, uint32_t expected_count) {
    struct timespec deadline;
    if (!getDeadline(&deadline)) {
        return false;
    }
    do {
        if (mtx_lock(&queue_ptr->mutex) != thrd_success) {
            return false;
        }
        bool ready = queue_ptr->waiter_count == expected_count;
        if (mtx_unlock(&queue_ptr->mutex) != thrd_success) {
            return false;
        }
        if (ready) {
            return true;
        }
        thrd_yield();
    } while (beforeDeadline(&deadline));
    return false;
}

static int waitOnce(void* arg_ptr) {
    WaitOperation* operation_ptr = arg_ptr;
    DOMINO_CODE code = dominoThreadQueueWait(operation_ptr->queue_ptr, operation_ptr->observed_epoch, &operation_ptr->deadline_utc);
    if (code != CODE_OK) {
        return code;
    }
    uint64_t epoch;
    code = dominoThreadQueueSnapshot(operation_ptr->queue_ptr, &epoch);
    if (code != CODE_OK) {
        return code;
    }
    return epoch != operation_ptr->observed_epoch && beforeDeadline(&operation_ptr->deadline_utc) ? CODE_OK : ERR_SYSTEM;
}

/** @brief 每个队列独立记录成功发布和控制唤醒；失败、出队及端点状态不推进版本。 */
static bool testQueueNotifications(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    DominoThreadQueue other_queue = {0};
    TestMessage message = {.value = 7U};
    uint64_t epoch;
    TEST_CHECK(
        dominoThreadQueueInit(&queue, sizeof(message), 1U, 1U) == CODE_OK && dominoThreadQueueInit(&other_queue, sizeof(message), 1U, 1U) == CODE_OK,
        "initialize independent queues");
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &epoch) == CODE_OK && epoch == 0U, "initial epoch is zero");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &message, false) == CODE_OK && message.id == 1U, "publish first message");
    TEST_CHECK(dominoThreadQueueWait(&queue, epoch, nullptr) == CODE_OK, "enqueue between Snapshot and Wait is not lost");
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &epoch) == CODE_OK && epoch == 1U, "successful enqueue advances epoch");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &message, false) == ERR_QUEUE_FULL, "reject full queue without a notification");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    TEST_CHECK(dominoThreadQueueProduce(&queue, &message, false) == ERR_QUEUE_PRODUCER_STOPPED, "reject stopped producer");
    TEST_CHECK(dominoThreadQueueWait(&queue, epoch, nullptr) == ERR_QUEUE_PRODUCER_STOPPED, "Wait observes stopped producer with pending data");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &message, false) == CODE_OK && message.value == 7U, "stopped producer still permits draining");
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &epoch) == CODE_OK && epoch == 1U,
               "failure, Stop, Resume and consumption do not publish notifications");
    dominoThreadQueueWakeAll(&queue);
    TEST_CHECK(dominoThreadQueueWait(&queue, epoch, nullptr) == CODE_OK, "control wake between Snapshot and Wait is not lost");
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &epoch) == CODE_OK && epoch == 2U, "WakeAll advances epoch without queued data");
    dominoThreadQueueWakeAll(&queue);
    dominoThreadQueueWakeAll(&queue);
    TEST_CHECK(dominoThreadQueueWait(&queue, epoch, nullptr) == CODE_OK, "multiple notifications before Wait can coalesce");
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &epoch) == CODE_OK && epoch == 4U, "snapshot sees the latest notification version");
    TEST_CHECK(dominoThreadQueueSnapshot(&other_queue, &epoch) == CODE_OK && epoch == 0U, "other queue is unaffected");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    TEST_CHECK(dominoThreadQueueWait(&queue, 0U, nullptr) == ERR_QUEUE_CONSUMER_STOPPED, "consumer stop takes precedence over epoch change");
    success = true;

cleanup:
    dominoThreadQueueDestroy(&other_queue);
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 多等待者都被唤醒；截止点等待不制造工作版本，销毁后可重新初始化。 */
static bool testWaitProtocol(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    thrd_t threads[2];
    uint32_t started_count = 0U;
    WaitOperation operations[2] = {{.queue_ptr = &queue}, {.queue_ptr = &queue}};
    uint64_t epoch;
    TEST_CHECK(dominoThreadQueueSnapshot(nullptr, &epoch) == ERR_NULL_POINTER && dominoThreadQueueSnapshot(&queue, nullptr) == ERR_NULL_POINTER &&
                   dominoThreadQueueWait(nullptr, 0U, nullptr) == ERR_NULL_POINTER,
               "reject null arguments");
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &epoch) == ERR_NOT_INITIALIZED && dominoThreadQueueWait(&queue, 0U, nullptr) == ERR_NOT_INITIALIZED,
               "reject operations before initialization");
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(TestMessage), 1U, 1U) == CODE_OK, "initialize queue");
    for (; started_count < 2U; started_count++) {
        TEST_CHECK(dominoThreadQueueSnapshot(&queue, &operations[started_count].observed_epoch) == CODE_OK &&
                       getDeadline(&operations[started_count].deadline_utc),
                   "snapshot before checking work");
        TEST_CHECK(thrd_create(&threads[started_count], waitOnce, &operations[started_count]) == thrd_success, "start waiter");
    }
    TEST_CHECK(waitForWaiters(&queue, 2U), "both consumers are asleep");
    TestMessage message = {.value = 53U};
    TEST_CHECK(dominoThreadQueueProduce(&queue, &message, false) == CODE_OK, "one enqueue wakes all version waiters");
    while (started_count > 0U) {
        int result;
        TEST_CHECK(thrd_join(threads[--started_count], &result) == thrd_success && result == CODE_OK, "notification wakes every waiter");
    }

    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &epoch) == CODE_OK, "snapshot before timeout");
    struct timespec deadline;
    TEST_CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC, "read timeout clock");
    deadline.tv_nsec += 10000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        deadline.tv_sec++;
    }
    TEST_CHECK(dominoThreadQueueWait(&queue, epoch, &deadline) == CODE_OK && !beforeDeadline(&deadline),
               "pending data alone does not prevent sleeping until a finite deadline");
    uint64_t after_epoch;
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &after_epoch) == CODE_OK && after_epoch == epoch, "timeout does not invent a notification");
    deadline.tv_nsec = 1000000000L;
    TEST_CHECK(dominoThreadQueueWait(&queue, epoch, &deadline) == ERR_INVALID_PARAM, "reject invalid UTC nanoseconds");
    dominoThreadQueueDestroy(&queue);
    TEST_CHECK(
        dominoThreadQueueInit(&queue, sizeof(TestMessage), 1U, 1U) == CODE_OK && dominoThreadQueueSnapshot(&queue, &epoch) == CODE_OK && epoch == 0U,
        "reinitialize after all waiters have joined");
    success = true;

cleanup:
    dominoThreadQueueWakeAll(&queue);
    while (started_count > 0U) {
        (void)thrd_join(threads[--started_count], nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

static int consumeOnce(void* arg_ptr) {
    DominoThreadQueue* queue_ptr = arg_ptr;
    TestMessage message;
    DOMINO_CODE code = dominoThreadQueueConsume(queue_ptr, &message, true);
    return code == CODE_OK && message.value != 61U ? ERR_SYSTEM : code;
}

/** @brief 阻塞消费和版本等待共用条件变量时，一次入队必须同时通知两类等待者。 */
static bool testMixedWaiters(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    thrd_t threads[2];
    uint32_t started_count = 0U;
    WaitOperation operation = {.queue_ptr = &queue};
    TestMessage message = {.value = 61U};
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(message), 1U, 1U) == CODE_OK, "initialize mixed wait queue");
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &operation.observed_epoch) == CODE_OK && getDeadline(&operation.deadline_utc), "snapshot epoch");
    TEST_CHECK(thrd_create(&threads[0], waitOnce, &operation) == thrd_success, "start version waiter");
    started_count++;
    TEST_CHECK(thrd_create(&threads[1], consumeOnce, &queue) == thrd_success, "start blocking consumer");
    started_count++;
    TEST_CHECK(waitForWaiters(&queue, 2U), "both types of waiter have registered");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &message, false) == CODE_OK, "publish one message to both types of waiter");
    while (started_count > 0U) {
        int result;
        TEST_CHECK(thrd_join(threads[--started_count], &result) == thrd_success && result == CODE_OK, "both waiter types make progress");
    }
    success = true;

cleanup:
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&queue);
    while (started_count > 0U) {
        (void)thrd_join(threads[--started_count], nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

static int waitForShutdown(void* arg_ptr) {
    WaitOperation* operation_ptr = arg_ptr;
    return dominoThreadQueueWait(operation_ptr->queue_ptr, operation_ptr->observed_epoch, nullptr);
}

/** @brief 显式关停和销毁都能结束已登记的无限版本等待，销毁不会提前释放条件变量。 */
static bool testWaitShutdown(bool destroy_while_waiting) {
    bool success = false;
    bool started = false;
    DominoThreadQueue queue = {0};
    thrd_t thread;
    WaitOperation operation = {.queue_ptr = &queue};
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(TestMessage), 1U, 1U) == CODE_OK &&
                   dominoThreadQueueSnapshot(&queue, &operation.observed_epoch) == CODE_OK,
               "initialize shutdown wait");
    TEST_CHECK(thrd_create(&thread, waitForShutdown, &operation) == thrd_success, "start infinite waiter");
    started = true;
    TEST_CHECK(waitForWaiters(&queue, 1U), "waiter has registered before shutdown");
    if (destroy_while_waiting) {
        dominoThreadQueueDestroy(&queue);
    } else {
        dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
        dominoThreadQueueWakeAll(&queue);
    }
    int result;
    TEST_CHECK(thrd_join(thread, &result) == thrd_success, "join shutdown waiter");
    started = false;
    TEST_CHECK(result == (destroy_while_waiting ? ERR_QUEUE_CONSUMER_STOPPED : ERR_QUEUE_PRODUCER_STOPPED), "return endpoint stop reason");
    success = true;

cleanup:
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&queue);
    if (started) {
        (void)thrd_join(thread, nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 模块拥有控制状态；暂停时不消费，恢复后处理消息，停止不依赖队列容量。 */
static int runControlledWorker(void* arg_ptr) {
    ControlledWorker* worker_ptr = arg_ptr;
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(worker_ptr->queue_ptr, &epoch);
        if (code != CODE_OK) {
            return code;
        }
        TestControl state = atomic_load(&worker_ptr->requested_state);
        atomic_store(&worker_ptr->observed_state, state);
        if (state == TEST_STOPPING) {
            return CODE_OK;
        }
        if (state == TEST_RUNNING) {
            TestMessage message;
            code = dominoThreadQueueConsume(worker_ptr->queue_ptr, &message, false);
            if (code == CODE_OK) {
                atomic_fetch_add(&worker_ptr->consumed_count, 1U);
                continue;
            }
            if (code != ERR_QUEUE_EMPTY) {
                return code;
            }
        }
        code = dominoThreadQueueWait(worker_ptr->queue_ptr, epoch, nullptr);
        if (code != CODE_OK) {
            return code;
        }
    }
}

static bool waitForWorker(ControlledWorker* worker_ptr, TestControl state, uint32_t consumed_count) {
    struct timespec deadline;
    if (!getDeadline(&deadline)) {
        return false;
    }
    do {
        if (atomic_load(&worker_ptr->observed_state) == state && atomic_load(&worker_ptr->consumed_count) == consumed_count) {
            return true;
        }
        thrd_yield();
    } while (beforeDeadline(&deadline));
    return false;
}

static bool testModuleControl(void) {
    bool success = false;
    bool started = false;
    DominoThreadQueue queue = {0};
    thrd_t thread;
    ControlledWorker worker = {.queue_ptr = &queue};
    atomic_init(&worker.requested_state, TEST_PAUSED);
    atomic_init(&worker.observed_state, TEST_RUNNING);
    atomic_init(&worker.consumed_count, 0U);
    TestMessage message = {.value = 41U};
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(message), 1U, 1U) == CODE_OK, "initialize controlled worker input");
    TEST_CHECK(thrd_create(&thread, runControlledWorker, &worker) == thrd_success, "start module worker");
    started = true;
    TEST_CHECK(waitForWorker(&worker, TEST_PAUSED, 0U) && waitForWaiters(&queue, 1U), "paused worker acknowledges and sleeps");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &message, false) == CODE_OK && dominoThreadQueueProduce(&queue, &message, false) == ERR_QUEUE_FULL,
               "fill business queue while paused");
    atomic_store(&worker.requested_state, TEST_RUNNING);
    dominoThreadQueueWakeAll(&queue);
    TEST_CHECK(waitForWorker(&worker, TEST_RUNNING, 1U) && waitForWaiters(&queue, 1U), "resume wakes the worker to drain pending data");
    atomic_store(&worker.requested_state, TEST_PAUSED);
    dominoThreadQueueWakeAll(&queue);
    TEST_CHECK(waitForWorker(&worker, TEST_PAUSED, 1U) && waitForWaiters(&queue, 1U), "pause wakes an empty-queue sleeper");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &message, false) == CODE_OK, "fill paused worker input again");
    atomic_store(&worker.requested_state, TEST_STOPPING);
    dominoThreadQueueWakeAll(&queue);
    int result;
    TEST_CHECK(thrd_join(thread, &result) == thrd_success, "join after module stop notification");
    started = false;
    TEST_CHECK(result == CODE_OK && atomic_load(&worker.consumed_count) == 1U, "full queue does not prevent stop or force consumption");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &message, false) == CODE_OK && message.value == 41U, "module retains ownership of pending data");
    success = true;

cleanup:
    atomic_store(&worker.requested_state, TEST_STOPPING);
    dominoThreadQueueWakeAll(&queue);
    if (started) {
        (void)thrd_join(thread, nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 真实计时线程直接 Produce，业务等待者无需额外轮询或手动通知。 */
static bool testTimerDelivery(void) {
    bool success = false;
    bool started = false;
    DominoThreadQueue queue = {0};
    thrd_t thread;
    WaitOperation operation = {.queue_ptr = &queue};
    dominoTimeModuleInitBefore();
    dominoTimeModuleInitAfter();
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(DominoThreadMessage), 2U, 2U) == CODE_OK && dominoTimerModuleInit() == CODE_OK,
               "initialize timer and target inbox");
    TEST_CHECK(dominoThreadQueueSnapshot(&queue, &operation.observed_epoch) == CODE_OK && getDeadline(&operation.deadline_utc),
               "snapshot before timer arrives");
    TEST_CHECK(thrd_create(&thread, waitOnce, &operation) == thrd_success, "start business waiter");
    started = true;
    TEST_CHECK(waitForWaiters(&queue, 1U), "business consumer is waiting on its inbox");
    DominoTimer request = {.event_queue_ptr = &queue, .delay_ms = 0U};
    TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK, "schedule without manually notifying business consumer");
    int result;
    TEST_CHECK(thrd_join(thread, &result) == thrd_success, "join timer event waiter");
    started = false;
    TEST_CHECK(result == CODE_OK, "timer delivery wakes consumer before its timeout");
    DominoTimer event;
    TEST_CHECK(testConsumeTimerMessage(&queue, &event, nullptr) == CODE_OK && event.timer_id == request.timer_id,
               "wakeup exposes the complete timer value");
    success = true;

cleanup:
    dominoTimerModuleExit();
    dominoThreadQueueWakeAll(&queue);
    if (started) {
        (void)thrd_join(thread, nullptr);
    }
    dominoTimeModuleExit();
    testDestroyMessageQueue(&queue);
    return success;
}

int main(void) {
    return testQueueNotifications() && testWaitProtocol() && testMixedWaiters() && testWaitShutdown(false) && testWaitShutdown(true) &&
                   testModuleControl() && testTimerDelivery()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
