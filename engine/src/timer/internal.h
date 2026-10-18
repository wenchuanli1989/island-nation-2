#ifndef DOMINO_ENGINE_TIMER_INTERNAL_H
#define DOMINO_ENGINE_TIMER_INTERNAL_H

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "../entry.h"
#include "../logger/entry.h"
#include "heap.h"

typedef struct DominoTimerModule {
    DominoThreadQueue command_queue;
    DominoTimerHeap heap;  ///< 所有业务队列共用的计时堆，按到期时间、timer_id 排序。
    mtx_t request_mutex;   ///< 保护请求转移、取消和唤醒状态；其后才取得队列锁。
    cnd_t wake_condition;
    thrd_t thread;
    DOMINO_CODE stop_result;          ///< 线程退出结果；线程写入后 release 发布 stopped_flag，Exit acquire 确认完成后读取。
    atomic_int failure_code;          ///< 已发布运行错误或退出同步首错；重复失败通知不再获取已不可信的请求锁。
    atomic_bool stop_requested_flag;  ///< Exit 持请求锁发布退出请求，随后唤醒线程。
    atomic_bool stopped_flag;         ///< 线程成功或失败退出前发布完成；Exit 确认结果成功后才 join。
    bool initialized_flag;
    bool wake_pending_flag;
} DominoTimerModule;

extern DominoTimerModule g_domino_timer;

/** @brief 工作线程及并发 API 的同步原语失效统一保持原有终止策略，包括线程退出阶段；资源 Init/Exit 不调用此检查。 */
static inline void checkThreadResult(int result, DOMINO_CODE code, const char* operation_ptr) {
    if (result != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "计时器同步失败: operation=%s, result=%d, code=%d", operation_ptr,
                          result, code);
        (void)fprintf(stderr, "domino timer: %s failed (result=%d, code=%d)\n", operation_ptr, result, code);
        abort();
    }
}

/** @brief 队列控制结果在 timer 调用链中成为致命错误时记录上下文；底层已记录的错误不重复诊断。 */
static inline void logTimerQueueFailure(DOMINO_CODE code, const char* operation_ptr) {
    if (code == ERR_QUEUE_FULL || code == ERR_QUEUE_PRODUCER_STOPPED || code == ERR_QUEUE_CONSUMER_STOPPED) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "计时器队列操作失败: operation=%s, code=%d", operation_ptr, code);
    }
}

/** @brief 持请求锁终结本模块并发布实例首错，唤醒计时线程；不回收共享资源。 */
static inline DOMINO_CODE failTimerLocked(DOMINO_CODE code) {
    int expected_code = CODE_OK;
    atomic_compare_exchange_strong(&g_domino_timer.failure_code, &expected_code, code);
    const DOMINO_CODE failure_code = dominoEngineReportFailure(code);
    atomic_store(&g_domino_timer.failure_code, failure_code);
    atomic_store_explicit(&g_domino_timer.stop_requested_flag, true, memory_order_release);
    checkThreadResult(cnd_broadcast(&g_domino_timer.wake_condition), ERR_CONDITION_BROADCAST, "cnd_broadcast");
    return (DOMINO_CODE)atomic_load(&g_domino_timer.failure_code);
}

/** @brief 持请求锁记录通知并唤醒计时线程；调用方未持请求锁或队列锁。 */
static inline void notifyTimerThread(void) {
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    g_domino_timer.wake_pending_flag = true;
    checkThreadResult(cnd_signal(&g_domino_timer.wake_condition), ERR_CONDITION_SIGNAL, "cnd_signal");
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
}

int dominoTimerThread(void* arg_ptr);

#endif
