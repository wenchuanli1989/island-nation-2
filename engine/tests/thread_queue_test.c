#include "queue/thread_queue.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#define TEST_PRODUCER_COUNT 3U
#define TEST_CONSUMER_COUNT 3U
#define TEST_CONSUMER_BATCH_MAX 4U
#define TEST_ITEMS_PER_PRODUCER 512U
#define TEST_ITEM_COUNT (TEST_PRODUCER_COUNT * TEST_ITEMS_PER_PRODUCER)

#define TEST_CHECK(condition, message)                              \
    do {                                                            \
        if (!(condition)) {                                         \
            (void)fprintf(stderr, "%s: %s\n", __func__, (message)); \
            goto cleanup;                                           \
        }                                                           \
    } while (0)

typedef struct QueueItem {
    uint64_t id;
    uint32_t payload;
} QueueItem;

typedef struct QueueOperation {
    DominoThreadQueue* queue_ptr;
    QueueItem item;
    bool produce;
} QueueOperation;

typedef struct QueueBatch {
    DominoThreadQueue* queue_ptr;
    _Atomic(uint32_t)* seen_ptr;
    _Atomic(uint64_t)* consumed_ids_ptr;
    uint64_t* assigned_ids_ptr;
    uint32_t first_index;
} QueueBatch;

/** @brief 等到操作已登记条件等待；使用实际队列状态而非睡眠猜测线程调度。 */
static bool waitForWaiters(DominoThreadQueue* queue_ptr, uint32_t expected_count) {
    struct timespec start_time;
    if (timespec_get(&start_time, TIME_UTC) != TIME_UTC) {
        return false;
    }
    for (;;) {
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
        struct timespec current_time;
        if (timespec_get(&current_time, TIME_UTC) != TIME_UTC || current_time.tv_sec - start_time.tv_sec >= 5) {
            return false;
        }
        thrd_yield();
    }
}

static int runOperation(void* argument_ptr) {
    QueueOperation* operation_ptr = argument_ptr;
    if (operation_ptr->produce) {
        return dominoThreadQueueProduce(operation_ptr->queue_ptr, &operation_ptr->item, true);
    }
    return dominoThreadQueueConsume(operation_ptr->queue_ptr, &operation_ptr->item, true);
}

static bool testCapacityAndFifo(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    QueueItem item = {.payload = 0U};
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 3U, 7U) == CODE_OK, "initialize queue");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == ERR_QUEUE_EMPTY, "empty nonblocking consume");
    for (item.payload = 0U; item.payload < 3U; item.payload++) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK && item.id == (uint64_t)item.payload + 1U,
                   "fill initial capacity with increasing IDs");
    }
    for (uint32_t expected = 0U; expected < 2U; expected++) {
        TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == CODE_OK && item.payload == expected && item.id == (uint64_t)expected + 1U,
                   "consume initial prefix with IDs");
    }
    for (item.payload = 3U; item.payload < 9U; item.payload++) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK && item.id == (uint64_t)item.payload + 1U,
                   "grow a wrapped ring through maximum capacity");
    }
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_QUEUE_FULL, "full nonblocking produce");
    for (uint32_t expected = 2U; expected < 9U; expected++) {
        TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == CODE_OK && item.payload == expected && item.id == (uint64_t)expected + 1U,
                   "FIFO and IDs survive ring growth");
    }
    TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == ERR_QUEUE_EMPTY, "fully drained queue is empty");
    success = true;

cleanup:
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 批量消费立即返回现有元素，跨环形边界保持 FIFO；停产后排空，停消费端禁止出队。 */
static bool testBatchConsumption(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    QueueItem item = {.payload = 0U};
    QueueItem outputs[TEST_CONSUMER_BATCH_MAX] = {0};
    uint32_t consumed_count = 99U;
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 5U, 5U) == CODE_OK, "initialize batch queue");
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, 0U, &consumed_count, false) == ERR_INVALID_PARAM && consumed_count == 0U,
               "reject zero batch limit and clear count");
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, nullptr, false) == ERR_NULL_POINTER,
               "reject missing batch count output");
    consumed_count = 99U;
    TEST_CHECK(
        dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, false) == ERR_QUEUE_EMPTY && consumed_count == 0U,
        "empty nonblocking batch reports zero items");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "supply one item for a partial blocking batch");
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, true) == CODE_OK && consumed_count == 1U &&
                   outputs[0].payload == 0U && outputs[0].id == 1U,
               "blocking batch does not wait to fill its limit");
    for (item.payload = 1U; item.payload < 6U; item.payload++) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "fill batch queue");
    }
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, 2U, &consumed_count, false) == CODE_OK && consumed_count == 2U &&
                   outputs[0].payload == 1U && outputs[1].payload == 2U,
               "batch limit leaves remaining items queued");
    for (item.payload = 6U; item.payload < 8U; item.payload++) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "wrap the ring before batch consumption");
    }
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, true) == CODE_OK &&
                   consumed_count == TEST_CONSUMER_BATCH_MAX,
               "producer stop still permits a full batch");
    for (uint32_t output_index = 0U; output_index < consumed_count; output_index++) {
        TEST_CHECK(outputs[output_index].payload == output_index + 3U && outputs[output_index].id == (uint64_t)output_index + 4U,
                   "batch copy across ring boundary preserves FIFO and IDs");
    }
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, true) == CODE_OK && consumed_count == 1U &&
                   outputs[0].payload == 7U && outputs[0].id == 8U,
               "producer stop permits the final partial batch");
    consumed_count = 99U;
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, false) == ERR_QUEUE_PRODUCER_STOPPED &&
                   consumed_count == 0U,
               "drained producer-stop batch reports zero items without blocking");
    consumed_count = 99U;
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, true) == ERR_QUEUE_PRODUCER_STOPPED &&
                   consumed_count == 0U,
               "drained producer-stop batch never waits");
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    item.payload = 8U;
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "enqueue before consumer stop");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_CONSUMER);
    consumed_count = 99U;
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, false) == ERR_QUEUE_CONSUMER_STOPPED &&
                   consumed_count == 0U,
               "consumer stop rejects batch consumption of queued data");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    consumed_count = 99U;
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, true) == ERR_QUEUE_CONSUMER_STOPPED &&
                   consumed_count == 0U,
               "consumer stop takes priority when both endpoints are stopped");
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_BOTH);
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_CONSUMER_BATCH_MAX, &consumed_count, false) == CODE_OK && consumed_count == 1U &&
                   outputs[0].payload == 8U && outputs[0].id == 9U,
               "stopped batch calls preserve queued data");
    success = true;

cleanup:
    dominoThreadQueueDestroy(&queue);
    return success;
}

static bool testStopEnds(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    QueueItem item = {.payload = 42U};
    QueueItem output = {0};
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 1U, 2U) == CODE_OK, "initialize producer-stop queue");
    TEST_CHECK(!dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH), "new queue has live endpoints");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "enqueue before producer stop");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_PRODUCER) &&
                   !dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_CONSUMER) && !dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH),
               "producer stop leaves consumer endpoint open");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_QUEUE_PRODUCER_STOPPED &&
                   dominoThreadQueueProduce(&queue, &item, true) == ERR_QUEUE_PRODUCER_STOPPED,
               "producer stop rejects both enqueue modes");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &output, true) == CODE_OK && output.payload == item.payload, "producer stop permits draining");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == ERR_QUEUE_PRODUCER_STOPPED &&
                   dominoThreadQueueConsume(&queue, &output, true) == ERR_QUEUE_PRODUCER_STOPPED,
               "drained producer-stop queue rejects both dequeue modes");
    dominoThreadQueueDestroy(&queue);

    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 1U, 2U) == CODE_OK, "initialize consumer-stop queue");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "enqueue before consumer stop");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_CONSUMER);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_CONSUMER) &&
                   !dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_PRODUCER) && !dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH),
               "consumer stop leaves producer endpoint open");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == ERR_QUEUE_CONSUMER_STOPPED &&
                   dominoThreadQueueConsume(&queue, &output, true) == ERR_QUEUE_CONSUMER_STOPPED,
               "consumer stop rejects queued data in both dequeue modes");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, true) == CODE_OK, "producer may grow after consumer stop");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_QUEUE_CONSUMER_STOPPED &&
                   dominoThreadQueueProduce(&queue, &item, true) == ERR_QUEUE_CONSUMER_STOPPED,
               "full consumer-stop queue never waits for an unavailable consumer");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH), "endpoint stops accumulate");
    dominoThreadQueueDestroy(&queue);

    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 1U, 2U) == CODE_OK, "initialize both-endpoint stop queue");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "enqueue before stopping both endpoints");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH), "explicit stop closes both endpoints");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_QUEUE_PRODUCER_STOPPED &&
                   dominoThreadQueueConsume(&queue, &output, false) == ERR_QUEUE_CONSUMER_STOPPED,
               "stopping both endpoints immediately rejects both operations even with queued data");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH), "repeated stop is idempotent");
    success = true;

cleanup:
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 恢复仅影响选中端，保留已扩容、回绕队列的元素顺序和容量。 */
static bool testResumeEnds(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    QueueItem item = {.payload = 0U};
    dominoThreadQueueResume(nullptr, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_BOTH);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH), "resume does not initialize a queue");
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 2U, 4U) == CODE_OK, "initialize resume queue");
    for (item.payload = 0U; item.payload < 4U; item.payload++) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "grow queue before stopping");
    }
    for (uint32_t expected = 0U; expected < 2U; expected++) {
        TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == CODE_OK && item.payload == expected, "advance ring head");
    }
    for (item.payload = 4U; item.payload < 6U; item.payload++) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "wrap ring before stopping");
    }
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&queue);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH), "explicit notification preserves stopped endpoints");
    dominoThreadQueueResume(&queue, (DominoThreadQueueEnd)99);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_BOTH), "invalid resume endpoint changes nothing");
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    TEST_CHECK(!dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_PRODUCER) && dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_CONSUMER),
               "producer resume leaves consumer stopped");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == ERR_QUEUE_CONSUMER_STOPPED, "consumer remains unavailable after producer resume");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_CONSUMER);
    TEST_CHECK(dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_PRODUCER) && !dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_CONSUMER),
               "consumer resume leaves producer stopped");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_QUEUE_PRODUCER_STOPPED, "producer remains unavailable after consumer resume");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == CODE_OK && item.payload == 2U, "resumed consumer drains preserved data");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_BOTH);
    TEST_CHECK(!dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_PRODUCER) && !dominoThreadQueueIsStopped(&queue, DOMINO_THREAD_QUEUE_CONSUMER),
               "explicit and repeated resume enable both endpoints");
    TEST_CHECK(queue.capacity == 4U && queue.max_capacity == 4U, "resume preserves allocated and maximum capacities");
    item.payload = 6U;
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK && item.id == 7U, "resumed producer appends without resetting IDs");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_QUEUE_FULL, "resumed queue retains its capacity limit");
    for (uint32_t expected = 3U; expected < 7U; expected++) {
        TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == CODE_OK && item.payload == expected && item.id == (uint64_t)expected + 1U,
                   "FIFO and IDs survive endpoint resume cycles");
    }
    TEST_CHECK(dominoThreadQueueConsume(&queue, &item, false) == ERR_QUEUE_EMPTY, "resumed drained queue is live and empty");
    success = true;

cleanup:
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 验证阻塞操作由对端正常操作唤醒后完成，并保持消息顺序。 */
static bool testBlockingTransfer(bool produce) {
    bool success = false;
    bool thread_active = false;
    DominoThreadQueue queue = {0};
    thrd_t worker;
    QueueOperation operation = {.queue_ptr = &queue, .item = {.payload = 29U}, .produce = produce};
    QueueItem item = {.payload = 11U};
    QueueItem output = {0};
    int result_code = 0;
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 1U, 1U) == CODE_OK, "initialize blocking queue");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_BOTH);
    if (produce) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "fill queue to block producer");
    }
    TEST_CHECK(thrd_create(&worker, runOperation, &operation) == thrd_success, "start blocking operation");
    thread_active = true;
    TEST_CHECK(waitForWaiters(&queue, 1U), "operation enters condition wait");
    if (produce) {
        TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == item.payload, "consumer releases queue slot");
    } else {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "producer supplies awaited item");
    }
    TEST_CHECK(thrd_join(worker, &result_code) == thrd_success, "join completed operation");
    thread_active = false;
    TEST_CHECK(result_code == CODE_OK, "awakened operation succeeds");
    if (produce) {
        TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == operation.item.payload,
                   "awakened producer publishes its item");
    } else {
        TEST_CHECK(operation.item.payload == item.payload && operation.item.id == item.id, "awakened consumer receives supplied item");
    }
    success = true;

cleanup:
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&queue);
    if (thread_active) {
        (void)thrd_join(worker, nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 一次批量消费释放多个空位，所有已阻塞的生产者均可继续入队。 */
static bool testBatchProducerWakeup(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    thrd_t workers[TEST_PRODUCER_COUNT];
    QueueOperation operations[TEST_PRODUCER_COUNT];
    QueueItem item = {0};
    QueueItem outputs[TEST_PRODUCER_COUNT] = {0};
    bool seen[TEST_PRODUCER_COUNT] = {false};
    uint32_t worker_count = 0U;
    uint32_t joined_count = 0U;
    uint32_t consumed_count = 0U;
    int result_code = 0;
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), TEST_PRODUCER_COUNT, TEST_PRODUCER_COUNT) == CODE_OK,
               "initialize batch producer wakeup queue");
    for (item.payload = 0U; item.payload < TEST_PRODUCER_COUNT; item.payload++) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "fill queue before producers wait");
    }
    for (; worker_count < TEST_PRODUCER_COUNT; worker_count++) {
        operations[worker_count] = (QueueOperation){.queue_ptr = &queue, .item = {.payload = TEST_PRODUCER_COUNT + worker_count}, .produce = true};
        TEST_CHECK(thrd_create(&workers[worker_count], runOperation, &operations[worker_count]) == thrd_success, "start blocked producer");
    }
    TEST_CHECK(waitForWaiters(&queue, TEST_PRODUCER_COUNT), "all producers wait before one batch releases their slots");
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_PRODUCER_COUNT, &consumed_count, false) == CODE_OK &&
                   consumed_count == TEST_PRODUCER_COUNT,
               "one batch releases space for all waiting producers");
    for (uint32_t output_index = 0U; output_index < consumed_count; output_index++) {
        TEST_CHECK(outputs[output_index].payload == output_index && outputs[output_index].id == (uint64_t)output_index + 1U,
                   "initial batch preserves FIFO");
    }
    TEST_CHECK(waitForWaiters(&queue, 0U), "batch notification wakes every producer without another consume");
    while (joined_count < worker_count) {
        TEST_CHECK(thrd_join(workers[joined_count], &result_code) == thrd_success, "join producer after batch wakeup");
        joined_count++;
        TEST_CHECK(result_code == CODE_OK, "every awakened producer succeeds");
    }
    TEST_CHECK(dominoThreadQueueConsumeBatch(&queue, outputs, TEST_PRODUCER_COUNT, &consumed_count, false) == CODE_OK &&
                   consumed_count == TEST_PRODUCER_COUNT,
               "every awakened producer publishes one item");
    for (uint32_t output_index = 0U; output_index < consumed_count; output_index++) {
        TEST_CHECK(outputs[output_index].payload >= TEST_PRODUCER_COUNT && outputs[output_index].payload < 2U * TEST_PRODUCER_COUNT &&
                       outputs[output_index].id == (uint64_t)TEST_PRODUCER_COUNT + output_index + 1U,
                   "awakened producer messages preserve FIFO IDs");
        uint32_t producer_index = outputs[output_index].payload - TEST_PRODUCER_COUNT;
        TEST_CHECK(!seen[producer_index], "each awakened producer message is consumed exactly once");
        seen[producer_index] = true;
    }
    success = true;

cleanup:
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&queue);
    for (; joined_count < worker_count; joined_count++) {
        (void)thrd_join(workers[joined_count], nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 显式关停并通知，或直接销毁时，已登记的阻塞生产/消费均须退出。 */
static bool testBlockedShutdown(bool produce, DominoThreadQueueEnd end, bool destroy) {
    bool success = false;
    bool thread_active = false;
    DominoThreadQueue queue = {0};
    thrd_t worker;
    QueueOperation operation = {.queue_ptr = &queue, .item = {.payload = 29U}, .produce = produce};
    int result_code = 0;
    DOMINO_CODE expected_code;
    if (produce) {
        expected_code = destroy || end != DOMINO_THREAD_QUEUE_CONSUMER ? ERR_QUEUE_PRODUCER_STOPPED : ERR_QUEUE_CONSUMER_STOPPED;
    } else {
        expected_code = destroy || end != DOMINO_THREAD_QUEUE_PRODUCER ? ERR_QUEUE_CONSUMER_STOPPED : ERR_QUEUE_PRODUCER_STOPPED;
    }
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(operation.item), 1U, 1U) == CODE_OK, "initialize shutdown queue");
    if (produce) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &operation.item, false) == CODE_OK, "fill queue to block producer");
    }
    TEST_CHECK(thrd_create(&worker, runOperation, &operation) == thrd_success, "start blocked operation");
    thread_active = true;
    TEST_CHECK(waitForWaiters(&queue, 1U), "operation is registered before shutdown");
    if (destroy) {
        dominoThreadQueueDestroy(&queue);
    } else {
        dominoThreadQueueStop(&queue, end);
        dominoThreadQueueWakeAll(&queue);
    }
    TEST_CHECK(thrd_join(worker, &result_code) == thrd_success, "join stopped operation");
    thread_active = false;
    TEST_CHECK(result_code == expected_code, "shutdown reports the endpoint preventing progress");
    if (!destroy) {
        dominoThreadQueueResume(&queue, end);
        QueueItem output = {0};
        if (produce) {
            TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == operation.item.payload,
                       "resume preserves queued item");
        }
        TEST_CHECK(dominoThreadQueueProduce(&queue, &operation.item, false) == CODE_OK, "new producer succeeds after waiter shutdown and resume");
        TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == operation.item.payload,
                   "new consumer succeeds after waiter shutdown and resume");
    }
    success = true;

cleanup:
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&queue);
    if (thread_active) {
        (void)thrd_join(worker, nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 单端关停后，对端正常传输唤醒旧等待者；等待者按停止状态拒绝改变数据。 */
static bool testStoppedWaiterAfterTransfer(bool produce) {
    bool success = false;
    bool thread_active = false;
    DominoThreadQueue queue = {0};
    thrd_t worker;
    QueueOperation operation = {.queue_ptr = &queue, .item = {.payload = 29U}, .produce = produce};
    QueueItem item = {.payload = 11U};
    QueueItem output = {0};
    int result_code = 0;
    DominoThreadQueueEnd end = produce ? DOMINO_THREAD_QUEUE_PRODUCER : DOMINO_THREAD_QUEUE_CONSUMER;
    DOMINO_CODE expected_code = produce ? ERR_QUEUE_PRODUCER_STOPPED : ERR_QUEUE_CONSUMER_STOPPED;
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 1U, 1U) == CODE_OK, "initialize stopped waiter queue");
    if (produce) {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "fill queue before producer waits");
    }
    TEST_CHECK(thrd_create(&worker, runOperation, &operation) == thrd_success, "start operation before endpoint stop");
    thread_active = true;
    TEST_CHECK(waitForWaiters(&queue, 1U), "operation waits before endpoint stop");
    dominoThreadQueueStop(&queue, end);
    if (produce) {
        TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == item.payload,
                   "live consumer releases space after producer stop");
    } else {
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "live producer supplies data after consumer stop");
    }
    TEST_CHECK(thrd_join(worker, &result_code) == thrd_success, "join waiter after opposite endpoint transfer");
    thread_active = false;
    TEST_CHECK(result_code == expected_code, "awakened waiter observes its stopped endpoint");
    dominoThreadQueueResume(&queue, end);
    if (!produce) {
        TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == item.payload,
                   "stopped consumer preserves the supplied item");
    }
    TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == ERR_QUEUE_EMPTY, "stopped waiter does not modify queue data");
    success = true;

cleanup:
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&queue);
    if (thread_active) {
        (void)thrd_join(worker, nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

static int resumeAfterStop(void* argument_ptr) {
    DominoThreadQueue* queue_ptr = argument_ptr;
    while (!dominoThreadQueueIsStopped(queue_ptr, DOMINO_THREAD_QUEUE_BOTH)) {
        thrd_yield();
    }
    dominoThreadQueueResume(queue_ptr, DOMINO_THREAD_QUEUE_BOTH);
    return CODE_OK;
}

/** @brief 关停后立即恢复，旧等待者可观察到停止或继续操作，消息总量不变。 */
static bool testConcurrentResume(bool produce) {
    bool success = false;
    bool resume_active = false;
    DominoThreadQueue queue = {0};
    thrd_t workers[TEST_PRODUCER_COUNT];
    thrd_t resumer;
    QueueOperation operations[TEST_PRODUCER_COUNT];
    uint32_t worker_count = 0U;
    uint32_t joined_count = 0U;
    QueueItem item = {.payload = 17U};
    QueueItem output = {0};
    int result_code = 0;
    const DOMINO_CODE expected_code = produce ? ERR_QUEUE_PRODUCER_STOPPED : ERR_QUEUE_CONSUMER_STOPPED;
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), TEST_PRODUCER_COUNT, TEST_PRODUCER_COUNT) == CODE_OK,
               "initialize concurrent resume queue");
    for (uint32_t cycle_index = 0U; cycle_index < 8U; cycle_index++) {
        worker_count = 0U;
        joined_count = 0U;
        uint32_t success_count = 0U;
        if (produce) {
            for (uint32_t item_index = 0U; item_index < TEST_PRODUCER_COUNT; item_index++) {
                TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "fill queue before concurrent shutdown");
            }
        }
        for (; worker_count < TEST_PRODUCER_COUNT; worker_count++) {
            operations[worker_count] = (QueueOperation){.queue_ptr = &queue, .item = {.payload = 29U}, .produce = produce};
            TEST_CHECK(thrd_create(&workers[worker_count], runOperation, &operations[worker_count]) == thrd_success, "start shutdown waiter");
        }
        TEST_CHECK(waitForWaiters(&queue, worker_count), "all operations wait before concurrent shutdown");
        TEST_CHECK(thrd_create(&resumer, resumeAfterStop, &queue) == thrd_success, "start concurrent resume");
        resume_active = true;
        dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
        TEST_CHECK(thrd_join(resumer, &result_code) == thrd_success, "join concurrent resume");
        resume_active = false;
        TEST_CHECK(result_code == CODE_OK, "concurrent resume completes");
        for (uint32_t item_index = 0U; item_index < worker_count; item_index++) {
            if (produce) {
                TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == item.payload,
                           "release enough slots while preserving original FIFO data");
            } else {
                TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "supply enough items for every resumed consumer");
            }
        }
        while (joined_count < worker_count) {
            TEST_CHECK(thrd_join(workers[joined_count], &result_code) == thrd_success, "join stopped or resumed waiter");
            joined_count++;
            TEST_CHECK(result_code == CODE_OK || result_code == expected_code, "waiter observes stopped or resumed state");
            if (result_code == CODE_OK) {
                success_count++;
                if (!produce) {
                    TEST_CHECK(operations[joined_count - 1U].item.payload == item.payload, "resumed consumer receives supplied data");
                }
            }
        }
        uint32_t remaining_count = produce ? success_count : worker_count - success_count;
        uint32_t expected_item = produce ? 29U : item.payload;
        for (uint32_t item_index = 0U; item_index < remaining_count; item_index++) {
            TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == expected_item,
                       "remaining data matches successful operations");
        }
        TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == ERR_QUEUE_EMPTY, "concurrent resume neither loses nor duplicates items");
        TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK, "produce after concurrent resume");
        TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.payload == item.payload, "consume after concurrent resume");
    }
    success = true;

cleanup:
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    if (resume_active) {
        (void)thrd_join(resumer, nullptr);
        dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    }
    dominoThreadQueueWakeAll(&queue);
    for (; joined_count < worker_count; joined_count++) {
        (void)thrd_join(workers[joined_count], nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

static int produceBatch(void* argument_ptr) {
    QueueBatch* batch_ptr = argument_ptr;
    uint64_t last_id = 0U;
    for (uint32_t item_index = batch_ptr->first_index; item_index < batch_ptr->first_index + TEST_ITEMS_PER_PRODUCER; item_index++) {
        QueueItem item = {.payload = item_index};
        DOMINO_CODE result_code = dominoThreadQueueProduce(batch_ptr->queue_ptr, &item, true);
        if (result_code != CODE_OK) {
            return result_code;
        }
        if (item.id <= last_id || item.id > TEST_ITEM_COUNT || item.payload != item_index) {
            return ERR_INVALID_DATA;
        }
        batch_ptr->assigned_ids_ptr[item_index] = item.id;
        last_id = item.id;
    }
    return CODE_OK;
}

static int consumeBatch(void* argument_ptr) {
    QueueBatch* batch_ptr = argument_ptr;
    for (;;) {
        QueueItem items[TEST_CONSUMER_BATCH_MAX] = {0};
        uint32_t consumed_count = 0U;
        DOMINO_CODE result_code = dominoThreadQueueConsumeBatch(batch_ptr->queue_ptr, items, TEST_CONSUMER_BATCH_MAX, &consumed_count, true);
        if (result_code == ERR_QUEUE_PRODUCER_STOPPED || result_code == ERR_QUEUE_CONSUMER_STOPPED) {
            return consumed_count == 0U ? CODE_OK : ERR_INVALID_DATA;
        }
        if (result_code != CODE_OK || consumed_count == 0U || consumed_count > TEST_CONSUMER_BATCH_MAX) {
            return ERR_INVALID_DATA;
        }
        for (uint32_t item_index = 0U; item_index < consumed_count; item_index++) {
            QueueItem* item_ptr = &items[item_index];
            if (item_ptr->payload >= TEST_ITEM_COUNT || item_ptr->id == 0U || item_ptr->id > TEST_ITEM_COUNT) {
                return ERR_INVALID_DATA;
            }
            (void)atomic_fetch_add_explicit(&batch_ptr->seen_ptr[item_ptr->payload], 1U, memory_order_relaxed);
            atomic_store_explicit(&batch_ptr->consumed_ids_ptr[item_ptr->payload], item_ptr->id, memory_order_relaxed);
        }
    }
}

static bool testConcurrentTransfers(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    _Atomic(uint32_t) seen[TEST_ITEM_COUNT] = {0};
    _Atomic(uint64_t) consumed_ids[TEST_ITEM_COUNT] = {0};
    uint64_t assigned_ids[TEST_ITEM_COUNT] = {0};
    bool seen_ids[TEST_ITEM_COUNT] = {false};
    thrd_t producers[TEST_PRODUCER_COUNT];
    thrd_t consumers[TEST_CONSUMER_COUNT];
    QueueBatch producer_batches[TEST_PRODUCER_COUNT];
    QueueBatch consumer_batch = {.queue_ptr = &queue, .seen_ptr = seen, .consumed_ids_ptr = consumed_ids};
    uint32_t producer_count = 0U;
    uint32_t consumer_count = 0U;
    uint32_t producer_joined_count = 0U;
    uint32_t consumer_joined_count = 0U;
    int result_code = 0;
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(QueueItem), 2U, 11U) == CODE_OK, "initialize concurrent queue");
    for (; consumer_count < TEST_CONSUMER_COUNT; consumer_count++) {
        TEST_CHECK(thrd_create(&consumers[consumer_count], consumeBatch, &consumer_batch) == thrd_success, "start consumer");
    }
    TEST_CHECK(waitForWaiters(&queue, TEST_CONSUMER_COUNT), "all consumers are waiting before producers start");
    for (; producer_count < TEST_PRODUCER_COUNT; producer_count++) {
        producer_batches[producer_count] =
            (QueueBatch){.queue_ptr = &queue, .assigned_ids_ptr = assigned_ids, .first_index = producer_count * TEST_ITEMS_PER_PRODUCER};
        TEST_CHECK(thrd_create(&producers[producer_count], produceBatch, &producer_batches[producer_count]) == thrd_success, "start producer");
    }
    while (producer_joined_count < producer_count) {
        TEST_CHECK(thrd_join(producers[producer_joined_count], &result_code) == thrd_success, "join producer");
        producer_joined_count++;
        TEST_CHECK(result_code == CODE_OK, "all producer items enqueue successfully");
    }
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    dominoThreadQueueWakeAll(&queue);
    while (consumer_joined_count < consumer_count) {
        TEST_CHECK(thrd_join(consumers[consumer_joined_count], &result_code) == thrd_success, "join consumer");
        consumer_joined_count++;
        TEST_CHECK(result_code == CODE_OK, "consumers terminate after draining");
    }
    for (uint32_t item_index = 0U; item_index < TEST_ITEM_COUNT; item_index++) {
        TEST_CHECK(atomic_load_explicit(&seen[item_index], memory_order_relaxed) == 1U, "each item is consumed exactly once");
        uint64_t id = assigned_ids[item_index];
        TEST_CHECK(id > 0U && id <= TEST_ITEM_COUNT && !seen_ids[id - 1U], "concurrent producers receive unique IDs");
        seen_ids[id - 1U] = true;
        TEST_CHECK(atomic_load_explicit(&consumed_ids[item_index], memory_order_relaxed) == id, "caller and queued copy carry the same ID");
    }
    success = true;

cleanup:
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&queue);
    for (; producer_joined_count < producer_count; producer_joined_count++) {
        (void)thrd_join(producers[producer_joined_count], nullptr);
    }
    for (; consumer_joined_count < consumer_count; consumer_joined_count++) {
        (void)thrd_join(consumers[consumer_joined_count], nullptr);
    }
    dominoThreadQueueDestroy(&queue);
    return success;
}

/** @brief 入队失败不修改消息或消耗 ID；重新初始化才重置编号。 */
static bool testIdErrorsAndLifetime(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    QueueItem item = {.id = 99U, .payload = 42U};
    QueueItem output = {0};
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(uint32_t), 1U, 1U) == ERR_INVALID_PARAM, "reject message smaller than its ID");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_NOT_INITIALIZED && item.id == 99U,
               "uninitialized enqueue preserves caller message");
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 1U, 1U) == CODE_OK, "initialize ID queue");
    TEST_CHECK(dominoThreadQueueProduce(nullptr, &item, false) == ERR_NULL_POINTER, "reject null queue");
    TEST_CHECK(dominoThreadQueueProduce(&queue, nullptr, false) == ERR_NULL_POINTER, "reject null message");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK && item.id == 1U && item.payload == 42U,
               "enqueue writes the first ID and preserves caller payload");
    item.id = 99U;
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_QUEUE_FULL && item.id == 99U && item.payload == 42U,
               "full queue preserves caller message");
    dominoThreadQueueStop(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == ERR_QUEUE_PRODUCER_STOPPED && item.id == 99U, "stopped enqueue preserves caller ID");
    dominoThreadQueueResume(&queue, DOMINO_THREAD_QUEUE_PRODUCER);
    TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.id == 1U && output.payload == 42U,
               "queued copy retains the assigned ID and payload");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK && item.id == 2U,
               "failed enqueue and stop/resume do not consume or reset IDs");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.id == item.id, "consume second ID");
    queue.next_id = UINT64_MAX - 1U;
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK && item.id == UINT64_MAX, "last ID is available");
    item.id = 99U;
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, true) == ERR_OUT_OF_RANGE && item.id == 99U,
               "exhausted IDs never wrap or block even when the queue is full");
    TEST_CHECK(dominoThreadQueueConsume(&queue, &output, false) == CODE_OK && output.id == UINT64_MAX, "consume final ID");
    dominoThreadQueueDestroy(&queue);
    TEST_CHECK(dominoThreadQueueInit(&queue, sizeof(item), 1U, 1U) == CODE_OK, "reinitialize ID queue");
    TEST_CHECK(dominoThreadQueueProduce(&queue, &item, false) == CODE_OK && item.id == 1U, "new lifecycle restarts IDs");
    success = true;

cleanup:
    dominoThreadQueueDestroy(&queue);
    return success;
}

static bool testCapacityOverflow(void) {
    bool success = false;
    DominoThreadQueue queue = {0};
    TEST_CHECK(dominoThreadQueueInit(&queue, SIZE_MAX, 2U, 2U) == ERR_INVALID_PARAM, "reject overflow of initial allocation size");
    TEST_CHECK(dominoThreadQueueInit(&queue, SIZE_MAX / 4U + 1U, 1U, 4U) == ERR_INVALID_PARAM, "reject overflow of future maximum allocation size");
    success = true;

cleanup:
    dominoThreadQueueDestroy(&queue);
    return success;
}

int main(void) {
    if (!testCapacityAndFifo() || !testBatchConsumption() || !testStopEnds() || !testResumeEnds() || !testCapacityOverflow() ||
        !testBlockingTransfer(false) || !testBlockingTransfer(true) || !testBatchProducerWakeup() || !testStoppedWaiterAfterTransfer(false) ||
        !testStoppedWaiterAfterTransfer(true)) {
        return EXIT_FAILURE;
    }
    const DominoThreadQueueEnd ends[] = {DOMINO_THREAD_QUEUE_BOTH, DOMINO_THREAD_QUEUE_PRODUCER, DOMINO_THREAD_QUEUE_CONSUMER};
    for (size_t end_index = 0U; end_index < sizeof(ends) / sizeof(ends[0]); end_index++) {
        if (!testBlockedShutdown(false, ends[end_index], false) || !testBlockedShutdown(true, ends[end_index], false)) {
            return EXIT_FAILURE;
        }
    }
    if (!testBlockedShutdown(false, DOMINO_THREAD_QUEUE_BOTH, true) || !testBlockedShutdown(true, DOMINO_THREAD_QUEUE_BOTH, true) ||
        !testConcurrentResume(false) || !testConcurrentResume(true) || !testConcurrentTransfers() || !testIdErrorsAndLifetime()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
