#include "../queue/thread_message.h"
#include "internal.h"

/**
 * @brief 在固定命令队列中按登记 ID 标记取消；找到返回 true，未找到返回 false。
 * @pre 调用方持有 request_mutex。
 */
static bool cancelGameTimerCommand(domino_game_timer_id_t game_timer_id) {
    DominoThreadQueue* queue_ptr = &g_domino_game_timer.command_queue;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&queue_ptr->mutex), ERR_MUTEX_LOCK, "mtx_lock");
    bool found_flag = false;
    DominoGameTimer* items_ptr = queue_ptr->items_ptr;
    uint32_t index = queue_ptr->head;
    for (uint32_t i = 0U; i < queue_ptr->count; ++i) {
        DominoGameTimer* game_timer_ptr = &items_ptr[index];
        if (game_timer_ptr->game_timer_id == game_timer_id) {
            game_timer_ptr->cancelled_flag = true;
            found_flag = true;
            break;
        }
        if (++index == queue_ptr->capacity) {
            index = 0U;
        }
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&queue_ptr->mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return found_flag;
}

/**
 * @brief 在指定业务队列中按类型和登记 ID 标记取消；找到返回 true，未找到返回 false。
 * @pre 调用方持有 request_mutex，且已确认队列初始化并存储 DominoThreadMessage。
 */
static bool cancelGameTimerEvent(DominoThreadQueue* event_queue_ptr, domino_game_timer_id_t game_timer_id) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&event_queue_ptr->mutex), ERR_MUTEX_LOCK, "mtx_lock");
    bool found_flag = false;
    DominoThreadMessage* items_ptr = event_queue_ptr->items_ptr;
    uint32_t index = event_queue_ptr->head;
    for (uint32_t i = 0U; i < event_queue_ptr->count; ++i) {
        DominoThreadMessage* message_ptr = &items_ptr[index];
        if (message_ptr->type == DOMINO_THREAD_MESSAGE_GAME_TIMER && message_ptr->payload.game_timer.game_timer_id == game_timer_id) {
            message_ptr->payload.game_timer.cancelled_flag = true;
            found_flag = true;
            break;
        }
        if (++index == event_queue_ptr->capacity) {
            index = 0U;
        }
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&event_queue_ptr->mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return found_flag;
}

DOMINO_CODE dominoGameTimerCancel(domino_game_timer_id_t game_timer_id, DominoThreadQueue* event_queue_ptr) {
    if (game_timer_id == 0U) {
        return ERR_INVALID_PARAM;
    }
    if (event_queue_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (event_queue_ptr->item_size != sizeof(DominoThreadMessage)) {
        return ERR_INVALID_PARAM;
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_game_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    bool found_flag = cancelGameTimerCommand(game_timer_id);
    if (found_flag) {
        goto unlock;
    }
    for (uint32_t i = 1U; i <= g_domino_game_timer.heap.count; ++i) {
        DominoGameTimer* game_timer_ptr = &g_domino_game_timer.heap.events_ptr[i];
        if (game_timer_ptr->game_timer_id == game_timer_id) {
            game_timer_ptr->cancelled_flag = true;
            found_flag = true;
            goto unlock;
        }
    }
    found_flag = cancelGameTimerEvent(event_queue_ptr, game_timer_id);
unlock:
    if (found_flag) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_signal(&g_domino_game_timer.wake_condition), ERR_CONDITION_SIGNAL, "cnd_signal");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_game_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return found_flag ? CODE_OK : ERR_NOT_FOUND;
}
