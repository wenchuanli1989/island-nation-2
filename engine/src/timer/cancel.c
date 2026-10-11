#include "internal.h"

/** @brief 调用方持请求锁；在队列锁内按登记 ID 和业务队列查找并标记请求。 */
static DOMINO_CODE cancelQueuedTimer(DominoThreadQueue* queue_ptr, domino_timer_id_t timer_id, DominoThreadQueue* event_queue_ptr,
                                     bool* found_out_ptr) {
    checkThreadResult(mtx_lock(&queue_ptr->mutex));
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
    checkThreadResult(mtx_unlock(&queue_ptr->mutex));
    return code;
}

DOMINO_CODE dominoTimerCancel(domino_timer_id_t timer_id, DominoThreadQueue* event_queue_ptr) {
    if (!g_domino_timer.initialized_flag) {
        return ERR_NOT_INITIALIZED;
    }
    if (timer_id == 0U) {
        return ERR_INVALID_PARAM;
    }
    if (event_queue_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (!event_queue_ptr->initialized) {
        return ERR_NOT_INITIALIZED;
    }
    if (event_queue_ptr->item_size != sizeof(DominoThreadMessage)) {
        return ERR_INVALID_PARAM;
    }
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
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
        checkThreadResult(cnd_signal(&g_domino_timer.wake_condition));
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    return code;
}
