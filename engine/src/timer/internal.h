#ifndef DOMINO_ENGINE_TIMER_INTERNAL_H
#define DOMINO_ENGINE_TIMER_INTERNAL_H

#include <threads.h>

#include "../entry.h"
#include "domino_shared_common.h"
#include "heap.h"

typedef struct DominoTimerModule {
    DominoThreadQueue command_queue;
    DominoTimerHeap heap;  ///< 所有业务队列共用的计时堆，按到期时间、timer_id 排序。
    mtx_t request_mutex;   ///< 保护请求转移、取消，并衔接命令检查与条件等待；其后才取得队列锁。
    cnd_t wake_condition;
    thrd_t thread;
} DominoTimerModule;

extern DominoTimerModule g_domino_timer;

/** @brief 持请求锁唤醒计时线程，与命令检查及条件等待衔接；调用方未持请求锁或队列锁。 */
static inline void notifyTimerThread(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_signal(&g_domino_timer.wake_condition), ERR_CONDITION_SIGNAL, "cnd_signal");
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
}

int dominoTimerThread(void* arg_ptr);

#endif
