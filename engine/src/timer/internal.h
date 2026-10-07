#ifndef DOMINO_ENGINE_TIMER_INTERNAL_H
#define DOMINO_ENGINE_TIMER_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "heap.h"

typedef struct DominoTimerModule {
    DominoThreadQueue command_queue;
    DominoTimerHeap heap;  ///< 所有业务队列共用的计时堆，按到期时间、timer_id 排序。
    mtx_t request_mutex;   ///< 保护请求转移、取消和唤醒状态；其后才取得队列锁。
    cnd_t wake_condition;
    thrd_t thread;
    bool initialized_flag;
    bool wake_pending_flag;
    bool stop_requested_flag;
} DominoTimerModule;

extern DominoTimerModule g_domino_timer;

/** @brief 与现有 queue 一样，同步原语自身失败属于不能继续执行的内部错误。 */
static inline void checkThreadResult(int result) {
    if (result != thrd_success) {
        (void)fprintf(stderr, "domino timer: synchronization failure (%d)\n", result);
        abort();
    }
}

/** @brief 项目队列返回码独立于标准线程返回码，不假定 CODE_OK 与 thrd_success 数值相同。 */
static inline void checkQueueResult(DOMINO_CODE code) {
    if (code != CODE_OK) {
        (void)fprintf(stderr, "domino timer: queue failure (%d)\n", code);
        abort();
    }
}

/** @brief 持请求锁记录通知并唤醒计时线程；调用方未持请求锁或队列锁。 */
static inline void notifyTimerThread(void) {
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    g_domino_timer.wake_pending_flag = true;
    checkThreadResult(cnd_signal(&g_domino_timer.wake_condition));
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
}

int dominoTimerThread(void* arg_ptr);

#endif
