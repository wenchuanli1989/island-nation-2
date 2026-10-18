#include "internal.h"

/** @brief 调用方持请求锁；在队列锁内按登记 ID 和业务队列查找并标记请求。 */
static DOMINO_CODE cancelQueuedTimer(DominoThreadQueue* queue_ptr, domino_timer_id_t timer_id, DominoThreadQueue* event_queue_ptr,
                                     bool* found_out_ptr) {
    checkThreadResult(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "mtx_lock");
    DOMINO_CODE code = ERR_NOT_FOUND;
    *found_out_ptr = false;
    bool command_flag = queue_ptr == &g_domino_timer.command_queue;
    uint32_t index = queue_ptr->head;
    for (uint32_t i = 0U; i < queue_ptr->count; ++i) {
        void* item_ptr = (uint8_t*)queue_ptr->items_ptr + (size_t)index * queue_ptr->item_size;
        DominoTimer* timer_ptr = nullptr;
        if (command_flag) {
            timer_ptr = item_ptr;
        } else {
            DominoThreadMessage* message_ptr = item_ptr;
            if (message_ptr->items_type == DOMINO_THREAD_MESSAGE_TIMER) {
                if (message_ptr->items_ptr == nullptr || message_ptr->items_size != sizeof(DominoTimer)) {
                    code = ERR_INVALID_MESSAGE;
                    break;
                }
                timer_ptr = message_ptr->items_ptr;
            }
        }
        if (timer_ptr != nullptr && (command_flag ? timer_ptr->id : timer_ptr->timer_id) == timer_id &&
            timer_ptr->event_queue_ptr == event_queue_ptr) {
            *found_out_ptr = true;
            if (!timer_ptr->cancelled_flag) {
                timer_ptr->cancelled_flag = true;
                code = CODE_OK;
            }
            break;
        }
        if (++index == queue_ptr->capacity) {
            index = 0U;
        }
    }
    checkThreadResult(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    if (code == ERR_INVALID_MESSAGE) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "取消计时器时遇到无效 TIMER 消息: 载荷为空或大小不匹配");
    }
    return code;
}

DOMINO_CODE dominoTimerCancel(domino_timer_id_t timer_id, DominoThreadQueue* event_queue_ptr) {
    if (!g_domino_timer.initialized_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "取消计时器失败: 计时器模块未初始化");
        return ERR_NOT_INITIALIZED;
    }
    const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
    if (failure_code != CODE_OK) {
        return failure_code;
    }
    if (timer_id == 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "取消计时器失败: timer_id 为 0");
        return ERR_INVALID_PARAM;
    }
    if (event_queue_ptr == nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "取消计时器失败: 业务队列指针为空");
        return ERR_NULL_POINTER;
    }
    if (!event_queue_ptr->initialized) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "取消计时器失败: 业务队列未初始化");
        return ERR_NOT_INITIALIZED;
    }
    if (event_queue_ptr->item_size != sizeof(DominoThreadMessage)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "取消计时器失败: 业务队列元素大小错误: item_size=%zu",
                          event_queue_ptr->item_size);
        return ERR_INVALID_PARAM;
    }
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    bool found_flag;
    DOMINO_CODE code = cancelQueuedTimer(&g_domino_timer.command_queue, timer_id, event_queue_ptr, &found_flag);
    if (found_flag) {
        goto unlock;
    }
    for (uint32_t i = 1U; i <= g_domino_timer.heap.count; ++i) {
        DominoTimer* timer_ptr = &g_domino_timer.heap.events_ptr[i];
        if (timer_ptr->timer_id == timer_id && timer_ptr->event_queue_ptr == event_queue_ptr) {
            if (!timer_ptr->cancelled_flag) {
                timer_ptr->cancelled_flag = true;
                code = CODE_OK;
            }
            goto unlock;
        }
    }
    code = cancelQueuedTimer(event_queue_ptr, timer_id, event_queue_ptr, &found_flag);
unlock:
    if (code == CODE_OK) {
        g_domino_timer.wake_pending_flag = true;
        checkThreadResult(cnd_signal(&g_domino_timer.wake_condition), ERR_CONDITION_SIGNAL, "cnd_signal");
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return code;
}
