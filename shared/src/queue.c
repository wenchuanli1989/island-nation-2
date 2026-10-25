#include <stdlib.h>
#include <string.h>

#include "domino_shared_common.h"
#include "domino_shared_queue.h"

/**
 * @brief 等待一次队列通知，并在等待前后更新等待者人数。
 * @pre 调用方已持有队列锁；等待期间会释放锁，返回前会重新取得锁。
 * @note 返回后，调用方仍须检查队列状态，不能把被唤醒当作条件已满足。
 *       等待失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
static inline void dominoThreadQueueWaitLocked(DominoThreadQueue* queue_ptr, cnd_t* condition_ptr, const char* what_str) {
    queue_ptr->waiter_count++;
    const int result_code = cnd_wait(condition_ptr, &queue_ptr->mutex);
    queue_ptr->waiter_count--;
    DOMINO_REQUIRE_THREAD_SUCCESS(result_code, ERR_CONDITION_WAIT, what_str);
}

/** @brief 调用方持锁；只在满队列且尚未达到最大容量时调用。
 * @pre Init 已校验 max_capacity * item_size 的范围；扩容不改变 item_size。
 */
static void dominoThreadQueueGrow(DominoThreadQueue* queue_ptr) {
    uint32_t new_capacity = queue_ptr->max_capacity;
    if (queue_ptr->capacity <= queue_ptr->max_capacity / 2U) {
        new_capacity = queue_ptr->capacity * 2U;
    }

    void* new_items_ptr = malloc((size_t)new_capacity * queue_ptr->item_size);
    if (new_items_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "queue growth allocation failed");
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
}

void dominoThreadQueueInit(DominoThreadQueue* queue_ptr, size_t item_size, uint32_t initial_capacity, uint32_t max_capacity) {
    if (queue_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "queue initialization requires a queue");
    }
    if (queue_ptr->initialized) {
        DOMINO_EXIT_WITH_ERROR(ERR_ALREADY_INITIALIZED, "queue initialization requires an uninitialized instance");
    }
    if (item_size < sizeof(uint64_t) || initial_capacity == 0U || initial_capacity > max_capacity) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "queue item size or capacity is invalid");
    }
    if ((size_t)max_capacity > SIZE_MAX / item_size) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "queue capacity multiplication overflows size_t");
    }

    /* 首次对象或上一轮 Destroy 已提供全零状态。 */
    queue_ptr->item_size = item_size;
    queue_ptr->capacity = initial_capacity;
    queue_ptr->max_capacity = max_capacity;
    queue_ptr->items_ptr = malloc((size_t)initial_capacity * item_size);
    if (queue_ptr->items_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "queue item allocation failed");
    }

    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_init(&queue_ptr->mutex, mtx_plain), ERR_MUTEX_INIT, "queue mutex initialization failed");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&queue_ptr->not_empty), ERR_CONDITION_INIT, "queue condition initialization failed");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&queue_ptr->not_full), ERR_CONDITION_INIT, "queue space condition initialization failed");
    queue_ptr->initialized = true;
}

DOMINO_CODE dominoThreadQueueSnapshot(DominoThreadQueue* queue_ptr, uint64_t* epoch_out_ptr) {
    if (queue_ptr == nullptr || epoch_out_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (!queue_ptr->initialized) {
        return ERR_NOT_INITIALIZED;
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "snapshot:mtx_lock");
    *epoch_out_ptr = queue_ptr->wake_epoch;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "snapshot:mtx_unlock");
    return CODE_OK;
}

DOMINO_CODE dominoThreadQueueWait(DominoThreadQueue* queue_ptr, uint64_t observed_epoch) {
    if (queue_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (!queue_ptr->initialized) {
        return ERR_NOT_INITIALIZED;
    }

    DOMINO_CODE result_code = CODE_OK;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "wait:mtx_lock");
    for (;;) {
        if (queue_ptr->flushing_flag) {
            result_code = ERR_QUEUE_FLUSHING;
            break;
        }
        if (queue_ptr->wake_epoch != observed_epoch) {
            break;
        }
        dominoThreadQueueWaitLocked(queue_ptr, &queue_ptr->not_empty, "wait:cnd_wait(not_empty)");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "wait:mtx_unlock");
    return result_code;
}

void dominoThreadQueueBeginFlush(DominoThreadQueue* queue_ptr) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return;
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "begin_flush:mtx_lock");

    queue_ptr->flushing_flag = true;
    queue_ptr->wake_epoch++;
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&queue_ptr->not_empty), ERR_CONDITION_BROADCAST, "begin_flush:cnd_broadcast(not_empty)");
    if (queue_ptr->producer_waiter_count > 0U) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&queue_ptr->not_full), ERR_CONDITION_BROADCAST, "begin_flush:cnd_broadcast(not_full)");
    }

    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "begin_flush:mtx_unlock");
}

void dominoThreadQueueWakeAll(DominoThreadQueue* queue_ptr) {
    if (queue_ptr == nullptr || !queue_ptr->initialized) {
        return;
    }

    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "wake_all:mtx_lock");
    queue_ptr->wake_epoch++;
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&queue_ptr->not_empty), ERR_CONDITION_BROADCAST, "wake_all:cnd_broadcast(not_empty)");
    if (queue_ptr->producer_waiter_count > 0U) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&queue_ptr->not_full), ERR_CONDITION_BROADCAST, "wake_all:cnd_broadcast(not_full)");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "wake_all:mtx_unlock");
}

void dominoThreadQueueDestroy(DominoThreadQueue* queue_ptr) {
    if (queue_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "queue destroy requires an instance");
    }
    if (!queue_ptr->initialized) {
        DOMINO_EXIT_WITH_ERROR(ERR_NOT_INITIALIZED, "queue destroy requires an initialized instance");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "destroy:mtx_lock");

    if (queue_ptr->waiter_count != 0U) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "queue destroy requires all waiters to exit");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "destroy:mtx_unlock");
    cnd_destroy(&queue_ptr->not_empty);
    cnd_destroy(&queue_ptr->not_full);
    mtx_destroy(&queue_ptr->mutex);
    free(queue_ptr->items_ptr);
    memset(queue_ptr, 0, sizeof(*queue_ptr));
}

/** @brief 两个入队公共入口共用此处一次参数及初始化校验；唤醒后仍须持锁复查动态条件。 */
__attribute__((hot)) static DOMINO_CODE dominoThreadQueueProduceInternal(DominoThreadQueue* queue_ptr, void* item_ptr, bool blocking) {
    if (queue_ptr == nullptr || item_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (!queue_ptr->initialized) {
        return ERR_NOT_INITIALIZED;
    }

    DOMINO_CODE result_code = CODE_OK;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "produce:mtx_lock");
    for (;;) {
        if (queue_ptr->flushing_flag) {
            result_code = ERR_QUEUE_FLUSHING;
            goto unlock;
        }
        if (queue_ptr->next_id == UINT64_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "queue message IDs are exhausted");
        }
        if (queue_ptr->count < queue_ptr->capacity) {
            break;
        }
        if (queue_ptr->capacity < queue_ptr->max_capacity) {
            dominoThreadQueueGrow(queue_ptr);
            break;
        }
        if (!blocking) {
            result_code = ERR_QUEUE_FULL;
            goto unlock;
        }
        queue_ptr->producer_waiter_count++;
        dominoThreadQueueWaitLocked(queue_ptr, &queue_ptr->not_full, "produce:cnd_wait(not_full)");
        queue_ptr->producer_waiter_count--;
    }

    uint64_t item_id = ++queue_ptr->next_id;
    memcpy(item_ptr, &item_id, sizeof(item_id));
    memcpy((uint8_t*)queue_ptr->items_ptr + ((size_t)queue_ptr->tail * queue_ptr->item_size), item_ptr, queue_ptr->item_size);
    if (++queue_ptr->tail == queue_ptr->capacity) {
        queue_ptr->tail = 0U;
    }
    queue_ptr->count++;
    queue_ptr->wake_epoch++;
    if (queue_ptr->waiter_count > queue_ptr->producer_waiter_count) {
        // 版本等待者与阻塞消费者共用条件变量，统一广播以便所有等待者重新检查条件。
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&queue_ptr->not_empty), ERR_CONDITION_BROADCAST, "produce:cnd_broadcast(not_empty)");
    }

unlock:
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "produce:mtx_unlock");
    return result_code;
}

DOMINO_CODE dominoThreadQueueProduce(DominoThreadQueue* queue_ptr, void* item_ptr) {
    return dominoThreadQueueProduceInternal(queue_ptr, item_ptr, false);
}

DOMINO_CODE dominoThreadQueueProduceBlocking(DominoThreadQueue* queue_ptr, void* item_ptr) {
    return dominoThreadQueueProduceInternal(queue_ptr, item_ptr, true);
}

__attribute__((hot)) DOMINO_CODE dominoThreadQueueConsume(DominoThreadQueue* queue_ptr, void* item_out_ptr, bool blocking) {
    /* 单条消费直接复用批量入口的一次公共校验与出队操作。 */
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
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "consume:mtx_lock");
    for (;;) {
        if (queue_ptr->count > 0U) {
            break;
        }
        if (queue_ptr->flushing_flag) {
            result_code = ERR_QUEUE_FLUSHING;
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
    if (queue_ptr->producer_waiter_count > 0U) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&queue_ptr->not_full), ERR_CONDITION_BROADCAST, "consume:cnd_broadcast(not_full)");
    }

unlock:
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "consume:mtx_unlock");
    return result_code;
}
