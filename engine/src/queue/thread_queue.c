#include "thread_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../logger/error.h"

static inline void dominoThreadQueueAssertSuccessAt(int result_code, DOMINO_CODE code, const char* what_str, const char* file_str, int line,
                                                    const char* function_str) {
    if (result_code == thrd_success) {
        return;
    }
    (void)dominoLogDirectErrorAt(code, what_str, file_str, line, function_str);
    abort();
}

#define dominoThreadQueueAssertSuccess(result_code, code, what_str) \
    dominoThreadQueueAssertSuccessAt((result_code), (code), (what_str), __FILE__, __LINE__, __func__)

/** @brief 仅关闭握手或排空中的同步错误可上报，普通运行仍维持原来的终止行为。 */
static inline bool dominoThreadQueueCheckSyncResultAt(DominoThreadQueue* queue_ptr, int result_code, DOMINO_CODE code, bool lifecycle_operation_flag,
                                                      const char* what_str, const char* file_str, int line, const char* function_str) {
    if (result_code == thrd_success) {
        return true;
    }
    if (lifecycle_operation_flag && atomic_load_explicit(&queue_ptr->lifecycle_stopping_flag, memory_order_acquire)) {
        (void)dominoLogDirectErrorAt(code, what_str, file_str, line, function_str);
        return false;
    }
    dominoThreadQueueAssertSuccessAt(result_code, code, what_str, file_str, line, function_str);
    return false;
}

#define dominoThreadQueueCheckSyncResult(queue_ptr, result_code, code, lifecycle_operation_flag, what_str) \
    dominoThreadQueueCheckSyncResultAt((queue_ptr), (result_code), (code), (lifecycle_operation_flag), (what_str), __FILE__, __LINE__, __func__)

/** @brief 持锁等待；登记等待者以跳过无接收方的通知，允许有限等待超时。 */
static inline int dominoThreadQueueWaitLocked(DominoThreadQueue* queue_ptr, cnd_t* condition_ptr, const struct timespec* deadline_utc_ptr,
                                              bool lifecycle_operation_flag, const char* what_str) {
    queue_ptr->waiter_count++;
    int result_code =
        deadline_utc_ptr == nullptr ? cnd_wait(condition_ptr, &queue_ptr->mutex) : cnd_timedwait(condition_ptr, &queue_ptr->mutex, deadline_utc_ptr);
    queue_ptr->waiter_count--;
    if (result_code != thrd_timedout) {
        (void)dominoThreadQueueCheckSyncResult(queue_ptr, result_code, ERR_CONDITION_WAIT, lifecycle_operation_flag, what_str);
    }
    return result_code;
}

/** @brief 调用方持锁；只在满队列且尚未达到最大容量时调用。 */
static DOMINO_CODE dominoThreadQueueGrow(DominoThreadQueue* queue_ptr) {
    uint32_t new_capacity = queue_ptr->max_capacity;
    if (queue_ptr->capacity <= queue_ptr->max_capacity / 2U) {
        new_capacity = queue_ptr->capacity * 2U;
    }

    void* new_items_ptr = malloc((size_t)new_capacity * queue_ptr->item_size);
    if (new_items_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_MEMORY_ALLOC, "queue growth allocation failed");
    }

    /* 按 head 两侧的连续区间恢复 FIFO 顺序。 */
    const size_t first_size = (size_t)(queue_ptr->capacity - queue_ptr->head) * queue_ptr->item_size;
    memcpy(new_items_ptr, (uint8_t*)queue_ptr->items_ptr + ((size_t)queue_ptr->head * queue_ptr->item_size), first_size);
    memcpy((uint8_t*)new_items_ptr + first_size, queue_ptr->items_ptr, (size_t)queue_ptr->head * queue_ptr->item_size);

    free(queue_ptr->items_ptr);
    queue_ptr->items_ptr = new_items_ptr;
    queue_ptr->head = 0U;
    queue_ptr->tail = queue_ptr->count;
    queue_ptr->capacity = new_capacity;
    return CODE_OK;
}

DOMINO_CODE dominoThreadQueueInit(DominoThreadQueue* queue_ptr, size_t item_size, uint32_t initial_capacity, uint32_t max_capacity) {
    if (queue_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "queue initialization requires a queue");
    }
    if (item_size < sizeof(uint64_t) || initial_capacity == 0U || initial_capacity > max_capacity) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "queue item size or capacity is invalid");
    }
    if ((size_t)max_capacity > SIZE_MAX / item_size) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_MEMORY_OVERFLOW, "queue capacity multiplication overflows size_t");
    }

    memset(queue_ptr, 0, sizeof(*queue_ptr));
    atomic_init(&queue_ptr->lifecycle_stopping_flag, false);
    queue_ptr->item_size = item_size;
    queue_ptr->capacity = initial_capacity;
    queue_ptr->max_capacity = max_capacity;
    queue_ptr->items_ptr = malloc((size_t)initial_capacity * item_size);
    if (queue_ptr->items_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_MEMORY_ALLOC, "queue item allocation failed");
    }

    if (mtx_init(&queue_ptr->mutex, mtx_plain) != thrd_success) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_MUTEX_INIT, "queue mutex initialization failed");
    }
    if (cnd_init(&queue_ptr->not_empty) != thrd_success) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_CONDITION_INIT, "queue condition initialization failed");
    }
    queue_ptr->initialized = true;
    return CODE_OK;
}

DOMINO_CODE dominoThreadQueueSnapshot(DominoThreadQueue* queue_ptr, uint64_t* epoch_out_ptr) {
    if (queue_ptr == nullptr || epoch_out_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "queue or required output pointer is null");
    }
    if (!queue_ptr->initialized) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NOT_INITIALIZED, "queue has not been initialized");
    }
    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "snapshot:mtx_lock");
    *epoch_out_ptr = queue_ptr->wake_epoch;
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "snapshot:mtx_unlock");
    return CODE_OK;
}

DOMINO_CODE dominoThreadQueueWait(DominoThreadQueue* queue_ptr, uint64_t observed_epoch, const struct timespec* deadline_utc_ptr) {
    if (queue_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "queue or required output pointer is null");
    }
    if (!queue_ptr->initialized) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NOT_INITIALIZED, "queue has not been initialized");
    }
    if (deadline_utc_ptr != nullptr && (deadline_utc_ptr->tv_nsec < 0L || deadline_utc_ptr->tv_nsec >= 1000000000L)) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "queue argument is invalid");
    }

    DOMINO_CODE result_code = CODE_OK;
    bool timed_out = false;
    if (!dominoThreadQueueCheckSyncResult(queue_ptr, mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, true, "wait:mtx_lock")) {
        return ERR_MUTEX_LOCK;
    }
    for (;;) {
        if (queue_ptr->consumer_stopped) {
            result_code = ERR_QUEUE_CONSUMER_STOPPED;
            break;
        }
        if (queue_ptr->producer_stopped) {
            result_code = ERR_QUEUE_PRODUCER_STOPPED;
            break;
        }
        if (queue_ptr->wake_epoch != observed_epoch || timed_out) {
            break;
        }
        const int wait_code = dominoThreadQueueWaitLocked(queue_ptr, &queue_ptr->not_empty, deadline_utc_ptr, true, "wait:cnd_wait(not_empty)");
        if (wait_code != thrd_success && wait_code != thrd_timedout) {
            result_code = ERR_CONDITION_WAIT;
            break;
        }
        timed_out = wait_code == thrd_timedout;
    }
    if (!dominoThreadQueueCheckSyncResult(queue_ptr, mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, true, "wait:mtx_unlock")) {
        return dominoErrorCodeIsSyncFailure(result_code) ? result_code : ERR_MUTEX_UNLOCK;
    }
    return result_code;
}

void dominoThreadQueueStop(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return;
    }
    if (end != DOMINO_THREAD_QUEUE_BOTH && end != DOMINO_THREAD_QUEUE_PRODUCER && end != DOMINO_THREAD_QUEUE_CONSUMER) {
        return;
    }

    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "stop:mtx_lock");
    if (end != DOMINO_THREAD_QUEUE_CONSUMER) {
        queue_ptr->producer_stopped = true;
    }
    if (end != DOMINO_THREAD_QUEUE_PRODUCER) {
        queue_ptr->consumer_stopped = true;
    }
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "stop:mtx_unlock");
}

DOMINO_CODE dominoThreadQueueStopAndWake(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return CODE_OK;
    }
    if (end != DOMINO_THREAD_QUEUE_BOTH && end != DOMINO_THREAD_QUEUE_PRODUCER && end != DOMINO_THREAD_QUEUE_CONSUMER) {
        return CODE_OK;
    }
    if (mtx_lock(&queue_ptr->mutex) != thrd_success) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_MUTEX_LOCK, "queue mutex lock failed");
    }

    atomic_store_explicit(&queue_ptr->lifecycle_stopping_flag, true, memory_order_release);
    if (end != DOMINO_THREAD_QUEUE_CONSUMER) {
        queue_ptr->producer_stopped = true;
    }
    if (end != DOMINO_THREAD_QUEUE_PRODUCER) {
        queue_ptr->consumer_stopped = true;
    }
    queue_ptr->wake_epoch++;
    DOMINO_CODE code = CODE_OK;
    if (cnd_broadcast(&queue_ptr->not_empty) != thrd_success) {
        code = DOMINO_LOG_DIRECT_ERROR(ERR_CONDITION_BROADCAST, "queue stop broadcast failed");
    }

    if (mtx_unlock(&queue_ptr->mutex) != thrd_success) {
        const DOMINO_CODE unlock_code = DOMINO_LOG_DIRECT_ERROR(ERR_MUTEX_UNLOCK, "queue mutex unlock failed");
        if (code == CODE_OK) {
            code = unlock_code;
        }
    }
    return code;
}

bool dominoThreadQueueIsLifecycleStopping(const DominoThreadQueue* queue_ptr) {
    return atomic_load_explicit(&queue_ptr->lifecycle_stopping_flag, memory_order_acquire);
}

void dominoThreadQueueResume(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return;
    }
    if (end != DOMINO_THREAD_QUEUE_BOTH && end != DOMINO_THREAD_QUEUE_PRODUCER && end != DOMINO_THREAD_QUEUE_CONSUMER) {
        return;
    }

    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "resume:mtx_lock");
    if (end != DOMINO_THREAD_QUEUE_CONSUMER) {
        queue_ptr->producer_stopped = false;
    }
    if (end != DOMINO_THREAD_QUEUE_PRODUCER) {
        queue_ptr->consumer_stopped = false;
    }
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "resume:mtx_unlock");
}

bool dominoThreadQueueIsStopped(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end) {
    if (end != DOMINO_THREAD_QUEUE_BOTH && end != DOMINO_THREAD_QUEUE_PRODUCER && end != DOMINO_THREAD_QUEUE_CONSUMER) {
        return false;
    }
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return true;
    }

    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "is_stopped:mtx_lock");
    bool stopped;
    if (end == DOMINO_THREAD_QUEUE_PRODUCER) {
        stopped = queue_ptr->producer_stopped;
    } else if (end == DOMINO_THREAD_QUEUE_CONSUMER) {
        stopped = queue_ptr->consumer_stopped;
    } else {
        stopped = ((queue_ptr->producer_stopped && queue_ptr->consumer_stopped) != 0);
    }
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "is_stopped:mtx_unlock");
    return stopped;
}

void dominoThreadQueueWakeAll(DominoThreadQueue* queue_ptr) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return;
    }

    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "wake_all:mtx_lock");
    queue_ptr->wake_epoch++;
    dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->not_empty), ERR_CONDITION_BROADCAST, "wake_all:cnd_broadcast(not_empty)");
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "wake_all:mtx_unlock");
}

DOMINO_CODE dominoThreadQueueDestroy(DominoThreadQueue* queue_ptr) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return CODE_OK;
    }
    if (mtx_lock(&queue_ptr->mutex) != thrd_success) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_MUTEX_LOCK, "queue mutex lock failed");
    }

    DOMINO_CODE code = CODE_OK;
    if (queue_ptr->waiter_count != 0U) {
        code = DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "queue destroy requires all waiters to exit");
    }
    if (mtx_unlock(&queue_ptr->mutex) != thrd_success) {
        const DOMINO_CODE unlock_code = DOMINO_LOG_DIRECT_ERROR(ERR_MUTEX_UNLOCK, "queue mutex unlock failed");
        if (code == CODE_OK) {
            code = unlock_code;
        }
    }
    if (code != CODE_OK) {
        return code;
    }
    cnd_destroy(&queue_ptr->not_empty);
    mtx_destroy(&queue_ptr->mutex);
    free(queue_ptr->items_ptr);
    memset(queue_ptr, 0, sizeof(*queue_ptr));
    return CODE_OK;
}

__attribute__((hot)) DOMINO_CODE dominoThreadQueueProduce(DominoThreadQueue* queue_ptr, void* item_ptr) {
    if (queue_ptr == nullptr || item_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "queue or required output pointer is null");
    }
    if (!queue_ptr->initialized) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NOT_INITIALIZED, "queue has not been initialized");
    }

    DOMINO_CODE result_code = CODE_OK;
    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "produce:mtx_lock");
    if (queue_ptr->producer_stopped) {
        result_code = ERR_QUEUE_PRODUCER_STOPPED;
        goto unlock;
    }
    if (queue_ptr->next_id == UINT64_MAX) {
        result_code = DOMINO_LOG_DIRECT_ERROR(ERR_ID_EXHAUSTED, "queue message IDs are exhausted");
        goto unlock;
    }
    if (queue_ptr->count == queue_ptr->capacity) {
        if (queue_ptr->capacity < queue_ptr->max_capacity) {
            result_code = dominoThreadQueueGrow(queue_ptr);
            if (result_code != CODE_OK) {
                goto unlock;
            }
        } else {
            result_code = queue_ptr->consumer_stopped ? ERR_QUEUE_CONSUMER_STOPPED : ERR_QUEUE_FULL;
            goto unlock;
        }
    }

    uint64_t item_id = ++queue_ptr->next_id;
    memcpy(item_ptr, &item_id, sizeof(item_id));
    memcpy((uint8_t*)queue_ptr->items_ptr + ((size_t)queue_ptr->tail * queue_ptr->item_size), item_ptr, queue_ptr->item_size);
    if (++queue_ptr->tail == queue_ptr->capacity) {
        queue_ptr->tail = 0U;
    }
    queue_ptr->count++;
    queue_ptr->wake_epoch++;
    if (queue_ptr->waiter_count > 0U) {
        // 版本等待者与阻塞消费者共用条件变量，统一广播以便所有等待者重新检查条件。
        dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->not_empty), ERR_CONDITION_BROADCAST, "produce:cnd_broadcast(not_empty)");
    }

unlock:
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "produce:mtx_unlock");
    return result_code;
}

__attribute__((hot)) DOMINO_CODE dominoThreadQueueConsume(DominoThreadQueue* queue_ptr, void* item_out_ptr, bool blocking) {
    uint32_t consumed_count;
    return dominoThreadQueueConsumeBatch(queue_ptr, item_out_ptr, 1U, &consumed_count, blocking);
}

__attribute__((hot)) DOMINO_CODE dominoThreadQueueConsumeBatch(DominoThreadQueue* queue_ptr, void* items_out_ptr, uint32_t max_count,
                                                               uint32_t* consumed_count_out_ptr, bool blocking) {
    if (consumed_count_out_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "queue or required output pointer is null");
    }
    *consumed_count_out_ptr = 0U;
    if (queue_ptr == nullptr || items_out_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "queue or required output pointer is null");
    }
    if (max_count == 0U) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "queue argument is invalid");
    }
    if (!queue_ptr->initialized) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NOT_INITIALIZED, "queue has not been initialized");
    }

    DOMINO_CODE result_code = CODE_OK;
    if (!dominoThreadQueueCheckSyncResult(queue_ptr, mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, true, "consume:mtx_lock")) {
        return ERR_MUTEX_LOCK;
    }
    for (;;) {
        if (queue_ptr->consumer_stopped) {
            result_code = ERR_QUEUE_CONSUMER_STOPPED;
            goto unlock;
        }
        if (queue_ptr->count > 0U) {
            break;
        }
        if (queue_ptr->producer_stopped) {
            result_code = ERR_QUEUE_PRODUCER_STOPPED;
            goto unlock;
        }
        if (!blocking) {
            result_code = ERR_QUEUE_EMPTY;
            goto unlock;
        }
        if (dominoThreadQueueWaitLocked(queue_ptr, &queue_ptr->not_empty, nullptr, true, "consume:cnd_wait(not_empty)") != thrd_success) {
            result_code = ERR_CONDITION_WAIT;
            goto unlock;
        }
    }

    const uint32_t consumed_count = queue_ptr->count < max_count ? queue_ptr->count : max_count;
    const uint32_t contiguous_count = queue_ptr->capacity - queue_ptr->head;
    const uint32_t first_count = consumed_count < contiguous_count ? consumed_count : contiguous_count;
    const uint32_t second_count = consumed_count - first_count;
    const size_t first_size = (size_t)first_count * queue_ptr->item_size;
    memcpy(items_out_ptr, (uint8_t*)queue_ptr->items_ptr + ((size_t)queue_ptr->head * queue_ptr->item_size), first_size);
    if (second_count > 0U) {
        memcpy((uint8_t*)items_out_ptr + first_size, queue_ptr->items_ptr, (size_t)second_count * queue_ptr->item_size);
    }
    queue_ptr->head += first_count;
    if (queue_ptr->head == queue_ptr->capacity) {
        queue_ptr->head = second_count;
    }
    queue_ptr->count -= consumed_count;
    *consumed_count_out_ptr = consumed_count;

unlock:
    if (!dominoThreadQueueCheckSyncResult(queue_ptr, mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, true, "consume:mtx_unlock")) {
        return dominoErrorCodeIsSyncFailure(result_code) ? result_code : ERR_MUTEX_UNLOCK;
    }
    return result_code;
}
