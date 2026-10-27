#include "entry.h"

#include <string.h>

#include "../queue/thread_message.h"
#include "../time/entry.h"
#include "internal.h"

DominoTimerModule g_domino_timer;

void dominoTimerModuleInit(void) {
    dominoThreadQueueInit(&g_domino_timer.command_queue, sizeof(DominoTimer), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_init(&g_domino_timer.request_mutex, mtx_plain), ERR_MUTEX_INIT, "mtx_init");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&g_domino_timer.wake_condition), ERR_CONDITION_INIT, "cnd_init");
}

void dominoTimerModuleStart(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_domino_timer.thread, dominoTimerThread, nullptr), ERR_THREAD_CREATE, "thrd_create");
}

void dominoTimerModuleStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_domino_timer.thread, nullptr), ERR_THREAD_JOIN, "thrd_join");
}

void dominoTimerModuleDestroy(void) {
    dominoThreadQueueDestroy(&g_domino_timer.command_queue);
    dominoTimerHeapDestroy(&g_domino_timer.heap);
    cnd_destroy(&g_domino_timer.wake_condition);
    mtx_destroy(&g_domino_timer.request_mutex);
    memset(&g_domino_timer, 0, sizeof(g_domino_timer));
}

void dominoTimerNotifyTimeChanged(void) {
    notifyTimerThread();
}

__attribute__((hot)) DOMINO_CODE dominoTimerScheduleAt(DominoThreadQueue* event_queue_ptr, domino_runtime_ms_t due_ms, uint8_t event_type,
                                                       uint64_t data_id, domino_timer_id_t* timer_id_out_ptr) {
    if (timer_id_out_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    *timer_id_out_ptr = 0U;
    if (event_queue_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (event_queue_ptr->item_size != sizeof(DominoThreadMessage)) {
        return ERR_INVALID_PARAM;
    }
    // 即使截止点已由业务确定，仍检查当前累计业务时间，不能在越界时继续登记。
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    DominoTimer timer = {
        .event_queue_ptr = event_queue_ptr,
        .data_id = data_id,
        .due_ms = due_ms,
        .event_type = event_type,
    };
    const DOMINO_CODE code = dominoThreadQueueProduce(&g_domino_timer.command_queue, &timer, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "提交计时器登记命令失败");
    }
    *timer_id_out_ptr = timer.timer_id;
    notifyTimerThread();
    return CODE_OK;
}
