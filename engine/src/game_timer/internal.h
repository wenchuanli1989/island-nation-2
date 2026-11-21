#ifndef DOMINO_ENGINE_GAME_TIMER_INTERNAL_H
#define DOMINO_ENGINE_GAME_TIMER_INTERNAL_H

#include <threads.h>

#include "../entry.h"
#include "domino_shared_common.h"
#include "heap.h"

/** @brief GameTimer 模块状态。 */
typedef struct DominoGameTimerModule {
    DominoThreadQueue command_queue;
    DominoGameTimerHeap heap;  ///< 全局最小堆，按 (due_ms, game_timer_id) 排序。
    mtx_t request_mutex;       ///< 请求锁：保护堆、命令迁移、取消和进入等待；持有它之后才取队列锁。
    cnd_t wake_condition;
    thrd_t thread;
} DominoGameTimerModule;

extern DominoGameTimerModule g_domino_game_timer;

/** @brief 持请求锁发送唤醒信号，保证信号不会落在计时线程检查完毕、开始等待之前（宪法 2.3）。 */
static inline void notifyGameTimerThread(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_game_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_signal(&g_domino_game_timer.wake_condition), ERR_CONDITION_SIGNAL, "cnd_signal");
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_game_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
}

/** @brief 计时线程主函数。 */
int dominoGameTimerThreadMain(void* arg_ptr);

#endif
