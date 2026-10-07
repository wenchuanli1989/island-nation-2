#include "entry.h"

#include <string.h>

#include "../time/entry.h"
#include "internal.h"

DominoTimerModule g_domino_timer;

DOMINO_CODE dominoTimerModuleInit(void) {
    if (g_domino_timer.initialized_flag) {
        return ERR_ALREADY_INITIALIZED;
    }
    memset(&g_domino_timer, 0, sizeof(g_domino_timer));
    DOMINO_CODE code = dominoThreadQueueInit(&g_domino_timer.command_queue, sizeof(DominoTimer), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                             DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
    if (code != CODE_OK) {
        return code;
    }
    code = ERR_MUTEX_INIT;
    if (mtx_init(&g_domino_timer.request_mutex, mtx_plain) != thrd_success) {
        goto destroy_command_queue;
    }
    code = ERR_SYSTEM;
    if (cnd_init(&g_domino_timer.wake_condition) != thrd_success) {
        goto destroy_request_mutex;
    }
    code = ERR_THREAD_CREATE;
    if (thrd_create(&g_domino_timer.thread, dominoTimerThread, nullptr) != thrd_success) {
        goto destroy_condition;
    }
    g_domino_timer.initialized_flag = true;
    return CODE_OK;

destroy_condition:
    cnd_destroy(&g_domino_timer.wake_condition);
destroy_request_mutex:
    mtx_destroy(&g_domino_timer.request_mutex);
destroy_command_queue:
    dominoThreadQueueDestroy(&g_domino_timer.command_queue);
    return code;
}

void dominoTimerModuleExit(void) {
    if (!g_domino_timer.initialized_flag) {
        return;
    }
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    g_domino_timer.stop_requested_flag = true;
    checkThreadResult(cnd_signal(&g_domino_timer.wake_condition));
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    checkThreadResult(thrd_join(g_domino_timer.thread, nullptr));
    dominoTimerHeapExit(&g_domino_timer.heap);
    dominoThreadQueueDestroy(&g_domino_timer.command_queue);
    cnd_destroy(&g_domino_timer.wake_condition);
    mtx_destroy(&g_domino_timer.request_mutex);
    g_domino_timer.initialized_flag = false;
}

void dominoTimerNotifyTimeChanged(void) {
    if (g_domino_timer.initialized_flag) {
        notifyTimerThread();
    }
}

__attribute__((hot)) DOMINO_CODE dominoTimerSchedule(DominoTimer* timer_ptr) {
    if (timer_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    timer_ptr->timer_id = 0U;
    if (!g_domino_timer.initialized_flag) {
        return ERR_NOT_INITIALIZED;
    }
    if (timer_ptr->cancelled_flag) {
        return ERR_INVALID_PARAM;
    }
    if (timer_ptr->event_queue_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (!timer_ptr->event_queue_ptr->initialized) {
        return ERR_NOT_INITIALIZED;
    }
    if (timer_ptr->event_queue_ptr->item_size != sizeof(DominoTimer)) {
        return ERR_INVALID_PARAM;
    }
    uint64_t now_ms = dominoTimeGetRuntimeMs();
    if (now_ms > UINT32_MAX || timer_ptr->delay_ms > UINT32_MAX - now_ms) {
        return ERR_OUT_OF_RANGE;
    }
    timer_ptr->due_ms = (uint32_t)now_ms + timer_ptr->delay_ms;
    DOMINO_CODE code = dominoThreadQueueProduce(&g_domino_timer.command_queue, timer_ptr, false);
    if (code == CODE_OK) {
        timer_ptr->timer_id = timer_ptr->id;
        notifyTimerThread();
    }
    return code;
}
