#include "entry.h"

#include <string.h>

#include "../queue/message.h"
#include "../time/entry.h"
#include "internal.h"

DominoGameTimerModule g_domino_game_timer;

void dominoGameTimerModuleInit(void) {
    dominoThreadQueueInit(&g_domino_game_timer.command_queue, sizeof(DominoGameTimerRequestMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                          DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_init(&g_domino_game_timer.request_mutex, mtx_plain), ERR_MUTEX_INIT, "mtx_init");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&g_domino_game_timer.wake_condition), ERR_CONDITION_INIT, "cnd_init");
}

void dominoGameTimerModuleStart(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_domino_game_timer.thread, dominoGameTimerThreadMain, nullptr), ERR_THREAD_CREATE, "thrd_create");
}

void dominoGameTimerModuleStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_domino_game_timer.thread, nullptr), ERR_THREAD_JOIN, "thrd_join");
}

void dominoGameTimerModuleDestroy(void) {
    // 登记只属于本会话：业务方下次加载后按持久化事实重新登记。
    dominoThreadQueueDestroy(&g_domino_game_timer.command_queue);
    dominoGameTimerHeapDestroy(&g_domino_game_timer.heap);
    cnd_destroy(&g_domino_game_timer.wake_condition);
    mtx_destroy(&g_domino_game_timer.request_mutex);
    memset(&g_domino_game_timer, 0, sizeof(g_domino_game_timer));
}

void dominoGameTimerNotifyTimeChanged(void) {
    notifyGameTimerThread();
}

__attribute__((hot)) DOMINO_CODE dominoGameTimerScheduleAt(DominoThreadQueue* event_queue_ptr, domino_runtime_ms_t due_ms, uint8_t event_type,
                                                           uint64_t target_id, domino_game_timer_id_t* game_timer_id_out_ptr) {
    if (game_timer_id_out_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    *game_timer_id_out_ptr = 0U;
    if (event_queue_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (event_queue_ptr->item_size != sizeof(DominoHumanInboxMessage)) {
        return ERR_INVALID_PARAM;
    }
    // 截止点由业务给定，但仍校验当前业务时间未越出 32 位范围。
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    DominoGameTimerRequestMessage request = {
        .event_queue_ptr = event_queue_ptr,
        .target_id = target_id,
        .due_ms = due_ms,
        .event_type = event_type,
    };
    const DOMINO_CODE code = dominoThreadQueueProduce(&g_domino_game_timer.command_queue, &request, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "提交计时器登记命令失败");
    }
    *game_timer_id_out_ptr = request.game_timer_id;
    notifyGameTimerThread();
    return CODE_OK;
}
