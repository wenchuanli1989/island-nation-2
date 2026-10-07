#include "thread_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void dominoThreadQueueAssertSuccess(int result_code, const char* what_str) {
    if (result_code == thrd_success) {
        return;
    }
    (void)fprintf(stderr, "domino thread queue: %s failed (rc=%d)\n", what_str, result_code);
    abort();
}

/** @brief 持锁等待数据或空位；登记等待者供销毁流程确认条件变量已无人使用。 */
static inline void dominoThreadQueueWaitLocked(DominoThreadQueue* queue_ptr, cnd_t* condition_ptr, const char* what_str) {
    queue_ptr->waiter_count++;
    int result_code = cnd_wait(condition_ptr, &queue_ptr->mutex);
    queue_ptr->waiter_count--;
    if (queue_ptr->waiter_count == 0U) {
        dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->waiters_drained), "wait:cnd_broadcast(waiters_drained)");
    }
    dominoThreadQueueAssertSuccess(result_code, what_str);
}

/** @brief 调用方持锁；只在满队列且尚未达到最大容量时调用。 */
static DOMINO_CODE dominoThreadQueueGrow(DominoThreadQueue* queue_ptr) {
    uint32_t new_capacity = queue_ptr->max_capacity;
    if (queue_ptr->capacity <= queue_ptr->max_capacity / 2U) {
        new_capacity = queue_ptr->capacity * 2U;
    }

    void* new_items_ptr = malloc((size_t)new_capacity * queue_ptr->item_size);
    if (new_items_ptr == nullptr) {
        return ERR_MEMORY_ALLOC;
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
        return ERR_NULL_POINTER;
    }
    if (item_size < sizeof(uint64_t) || initial_capacity == 0U || initial_capacity > max_capacity || (size_t)max_capacity > SIZE_MAX / item_size) {
        return ERR_INVALID_PARAM;
    }

    memset(queue_ptr, 0, sizeof(*queue_ptr));
    queue_ptr->item_size = item_size;
    queue_ptr->capacity = initial_capacity;
    queue_ptr->max_capacity = max_capacity;
    queue_ptr->items_ptr = malloc((size_t)initial_capacity * item_size);
    if (queue_ptr->items_ptr == nullptr) {
        return ERR_MEMORY_ALLOC;
    }

    dominoThreadQueueAssertSuccess(mtx_init(&queue_ptr->mutex, mtx_plain), "init:mtx_init(mutex)");
    dominoThreadQueueAssertSuccess(cnd_init(&queue_ptr->not_empty), "init:cnd_init(not_empty)");
    dominoThreadQueueAssertSuccess(cnd_init(&queue_ptr->not_full), "init:cnd_init(not_full)");
    dominoThreadQueueAssertSuccess(cnd_init(&queue_ptr->waiters_drained), "init:cnd_init(waiters_drained)");
    queue_ptr->initialized = true;
    return CODE_OK;
}

void dominoThreadQueueStop(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return;
    }
    if (end != DOMINO_THREAD_QUEUE_BOTH && end != DOMINO_THREAD_QUEUE_PRODUCER && end != DOMINO_THREAD_QUEUE_CONSUMER) {
        return;
    }

    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), "stop:mtx_lock");
    if (end != DOMINO_THREAD_QUEUE_CONSUMER) {
        queue_ptr->producer_stopped = true;
    }
    if (end != DOMINO_THREAD_QUEUE_PRODUCER) {
        queue_ptr->consumer_stopped = true;
    }
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), "stop:mtx_unlock");
}

void dominoThreadQueueResume(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return;
    }
    if (end != DOMINO_THREAD_QUEUE_BOTH && end != DOMINO_THREAD_QUEUE_PRODUCER && end != DOMINO_THREAD_QUEUE_CONSUMER) {
        return;
    }

    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), "resume:mtx_lock");
    if (end != DOMINO_THREAD_QUEUE_CONSUMER) {
        queue_ptr->producer_stopped = false;
    }
    if (end != DOMINO_THREAD_QUEUE_PRODUCER) {
        queue_ptr->consumer_stopped = false;
    }
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), "resume:mtx_unlock");
}

bool dominoThreadQueueIsStopped(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end) {
    if (end != DOMINO_THREAD_QUEUE_BOTH && end != DOMINO_THREAD_QUEUE_PRODUCER && end != DOMINO_THREAD_QUEUE_CONSUMER) {
        return false;
    }
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return true;
    }

    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), "is_stopped:mtx_lock");
    bool stopped;
    if (end == DOMINO_THREAD_QUEUE_PRODUCER) {
        stopped = queue_ptr->producer_stopped;
    } else if (end == DOMINO_THREAD_QUEUE_CONSUMER) {
        stopped = queue_ptr->consumer_stopped;
    } else {
        stopped = ((queue_ptr->producer_stopped && queue_ptr->consumer_stopped) != 0);
    }
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), "is_stopped:mtx_unlock");
    return stopped;
}

void dominoThreadQueueWakeAll(DominoThreadQueue* queue_ptr) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return;
    }

    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), "wake_all:mtx_lock");
    dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->not_empty), "wake_all:cnd_broadcast(not_empty)");
    dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->not_full), "wake_all:cnd_broadcast(not_full)");
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), "wake_all:mtx_unlock");
}

void dominoThreadQueueDestroy(DominoThreadQueue* queue_ptr) {
    if (queue_ptr == nullptr) {
        return;
    }

    if (queue_ptr->initialized) {
        dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), "destroy:mtx_lock");
        queue_ptr->producer_stopped = true;
        queue_ptr->consumer_stopped = true;
        dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->not_empty), "destroy:cnd_broadcast(not_empty)");
        dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->not_full), "destroy:cnd_broadcast(not_full)");
        while (queue_ptr->waiter_count > 0U) {
            dominoThreadQueueAssertSuccess(cnd_wait(&queue_ptr->waiters_drained, &queue_ptr->mutex), "destroy:cnd_wait(waiters_drained)");
        }
        dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), "destroy:mtx_unlock");
        cnd_destroy(&queue_ptr->not_empty);
        cnd_destroy(&queue_ptr->not_full);
        cnd_destroy(&queue_ptr->waiters_drained);
        mtx_destroy(&queue_ptr->mutex);
    }

    free(queue_ptr->items_ptr);
    memset(queue_ptr, 0, sizeof(*queue_ptr));
}

__attribute__((hot)) DOMINO_CODE dominoThreadQueueProduce(DominoThreadQueue* queue_ptr, void* item_ptr, bool blocking) {
    if (queue_ptr == nullptr || item_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (!queue_ptr->initialized) {
        return ERR_NOT_INITIALIZED;
    }

    DOMINO_CODE result_code = CODE_OK;
    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), "produce:mtx_lock");
    for (;;) {
        if (queue_ptr->producer_stopped) {
            result_code = ERR_QUEUE_PRODUCER_STOPPED;
            goto unlock;
        }
        if (queue_ptr->next_id == UINT64_MAX) {
            result_code = ERR_OUT_OF_RANGE;
            goto unlock;
        }
        if (queue_ptr->count < queue_ptr->capacity) {
            break;
        }
        if (queue_ptr->capacity < queue_ptr->max_capacity) {
            result_code = dominoThreadQueueGrow(queue_ptr);
            if (result_code != CODE_OK) {
                goto unlock;
            }
            break;
        }
        if (queue_ptr->consumer_stopped) {
            result_code = ERR_QUEUE_CONSUMER_STOPPED;
            goto unlock;
        }
        if (!blocking) {
            result_code = ERR_QUEUE_FULL;
            goto unlock;
        }
        dominoThreadQueueWaitLocked(queue_ptr, &queue_ptr->not_full, "produce:cnd_wait(not_full)");
    }

    uint64_t item_id = ++queue_ptr->next_id;
    memcpy(item_ptr, &item_id, sizeof(item_id));
    memcpy((uint8_t*)queue_ptr->items_ptr + ((size_t)queue_ptr->tail * queue_ptr->item_size), item_ptr, queue_ptr->item_size);
    if (++queue_ptr->tail == queue_ptr->capacity) {
        queue_ptr->tail = 0U;
    }
    queue_ptr->count++;
    if (queue_ptr->waiter_count > 0U) {
        dominoThreadQueueAssertSuccess(cnd_signal(&queue_ptr->not_empty), "produce:cnd_signal(not_empty)");
        if (queue_ptr->next_id == UINT64_MAX) {
            // 编号耗尽后唤醒剩余生产者，避免继续等待空位。
            dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->not_full), "produce:cnd_broadcast(not_full)");
        }
    }

unlock:
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), "produce:mtx_unlock");
    return result_code;
}

__attribute__((hot)) DOMINO_CODE dominoThreadQueueConsume(DominoThreadQueue* queue_ptr, void* item_out_ptr, bool blocking) {
    uint32_t consumed_count;
    return dominoThreadQueueConsumeBatch(queue_ptr, item_out_ptr, 1U, &consumed_count, blocking);
}

__attribute__((hot)) DOMINO_CODE dominoThreadQueueConsumeBatch(DominoThreadQueue* queue_ptr, void* items_out_ptr, uint32_t max_count,
                                                               uint32_t* consumed_count_out_ptr, bool blocking) {
    if (consumed_count_out_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    *consumed_count_out_ptr = 0U;
    if (queue_ptr == nullptr || items_out_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (max_count == 0U) {
        return ERR_INVALID_PARAM;
    }
    if (!queue_ptr->initialized) {
        return ERR_NOT_INITIALIZED;
    }

    DOMINO_CODE result_code = CODE_OK;
    dominoThreadQueueAssertSuccess(mtx_lock(&queue_ptr->mutex), "consume:mtx_lock");
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
        dominoThreadQueueWaitLocked(queue_ptr, &queue_ptr->not_empty, "consume:cnd_wait(not_empty)");
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
    if (queue_ptr->waiter_count > 0U) {
        if (consumed_count == 1U) {
            dominoThreadQueueAssertSuccess(cnd_signal(&queue_ptr->not_full), "consume:cnd_signal(not_full)");
        } else {
            dominoThreadQueueAssertSuccess(cnd_broadcast(&queue_ptr->not_full), "consume:cnd_broadcast(not_full)");
        }
    }

unlock:
    dominoThreadQueueAssertSuccess(mtx_unlock(&queue_ptr->mutex), "consume:mtx_unlock");
    return result_code;
}
