#include <time.h>

#include "../time/entry.h"
#include "internal.h"

#define DOMINO_TIMER_BATCH_SIZE 64U

/** @brief 在批次边界和等待谓词中统一观察引擎退出及 timer 自身退出请求。 */
static inline bool isTimerStopRequested(void) {
    return dominoEngineIsStopRequested() || atomic_load_explicit(&g_domino_timer.stop_requested_flag, memory_order_acquire);
}

/**
 * @brief 整批命令的出队与入堆共用一次请求锁，取消查找不会漏掉转移中的请求。
 * @return 成功返回 CODE_OK；失败记录实例首错并停止迁移。输出实际出队数量，包括取消项。
 */
static DOMINO_CODE processCommandBatch(uint32_t* command_count_out_ptr) {
    uint32_t command_count = 0U;
    DOMINO_CODE code = CODE_OK;
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    while (command_count < DOMINO_TIMER_BATCH_SIZE) {
        code = dominoEngineGetLifecycleError();
        if (code != CODE_OK) {
            break;
        }
        DominoTimer timer;
        code = dominoThreadQueueConsume(&g_domino_timer.command_queue, &timer, false);
        if (code == ERR_QUEUE_EMPTY) {
            code = CODE_OK;
            break;
        }
        if (code != CODE_OK) {
            logTimerQueueFailure(code, "读取登记命令");
            break;
        }
        command_count++;
        if (timer.cancelled_flag) {
            continue;
        }
        timer.timer_id = timer.id;
        code = dominoTimerHeapPush(&g_domino_timer.heap, &timer);
        if (code != CODE_OK) {
            break;
        }
    }
    if (code != CODE_OK) {
        code = failTimerLocked(code);
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    *command_count_out_ptr = command_count;
    return code;
}

/**
 * @brief 每批最多 64 次回收或非阻塞投递；失败释放未交付载荷并终结实例。
 * @param full_out_ptr 输出是否耗尽本批预算，不代表确认仍有可处理的堆项。
 */
static DOMINO_CODE processHeapBatch(bool* full_out_ptr) {
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    domino_runtime_ms_t now_ms;
    DOMINO_CODE code = dominoTimeGetRuntimeMs(&now_ms);
    uint32_t work_count = 0U;
    if (code != CODE_OK) {
        goto unlock;
    }
    while (work_count < DOMINO_TIMER_BATCH_SIZE) {
        code = dominoEngineGetLifecycleError();
        if (code != CODE_OK) {
            break;
        }
        const DominoTimer* timer_ptr = dominoTimerHeapPeek(&g_domino_timer.heap);
        if (timer_ptr == nullptr) {
            break;
        }
        // 已取消堆顶立即回收；不等待原截止点，也不占用目标事件队列。
        if (timer_ptr->cancelled_flag) {
            dominoTimerHeapPop(&g_domino_timer.heap);
            work_count++;
            continue;
        }
        if (timer_ptr->due_ms > now_ms) {
            break;
        }
        // 取消检查、堆顶移除和事件入队共用请求锁，Cancel 无法在迁移中插入。
        // 收件队列复制通用消息描述符；载荷独立于堆及当前线程的栈。
        DominoTimer timer = *timer_ptr;
        dominoTimerHeapPop(&g_domino_timer.heap);
        DominoThreadMessage message = {0};
        code = dominoThreadMessageInitCopy(&message, DOMINO_THREAD_MESSAGE_TIMER, &timer, sizeof(timer));
        if (code == CODE_OK) {
            code = dominoThreadQueueProduce(timer.event_queue_ptr, &message);
        }
        if (code != CODE_OK) {
            logTimerQueueFailure(code, "投递到期事件");
            dominoThreadMessageRelease(&message);
            break;
        }
        // 入队成功后载荷归接收方；此处不再访问或释放 message.items_ptr。
        work_count++;
    }
unlock:
    if (code != CODE_OK) {
        code = failTimerLocked(code);
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    *full_out_ptr = work_count == DOMINO_TIMER_BATCH_SIZE;
    return code;
}

static inline DOMINO_CODE getWaitDeadline(domino_runtime_ms_t delta_ms, struct timespec* deadline_out_ptr) {
    // 标准 cnd_timedwait 使用 TIME_UTC；唤醒后仍按累计未冻结的现实时间判定到期。
    if (timespec_get(deadline_out_ptr, TIME_UTC) != TIME_UTC) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "计算计时器等待截止点失败: UTC 时钟采样失败");
        return ERR_CLOCK_READ;
    }
    uint32_t wait_s = delta_ms / UINT32_C(1000);
    uint32_t wait_ns = (delta_ms % UINT32_C(1000)) * UINT32_C(1000000);
    wait_ns += (uint32_t)deadline_out_ptr->tv_nsec;
    deadline_out_ptr->tv_sec += (time_t)(wait_s + wait_ns / UINT32_C(1000000000));
    deadline_out_ptr->tv_nsec = (long)(wait_ns % UINT32_C(1000000000));
    return CODE_OK;
}

/**
 * @brief 持请求锁决定等待；期间收到通知则返回主循环，条件等待自动释放请求锁。
 * @return 正常结束本次等待返回 CODE_OK；业务时钟或截止点计算失败返回当前错误。
 */
static DOMINO_CODE waitForWork(void) {
    struct timespec deadline = {0};
    bool timed_wait_flag = false;
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    DOMINO_CODE code = dominoEngineGetLifecycleError();
    if (code != CODE_OK) {
        goto failed;
    }
    if (isTimerStopRequested()) {
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
        return CODE_OK;
    }
    if (g_domino_timer.wake_pending_flag || dominoTimeModuleIsFrozen()) {
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
        return CODE_OK;
    }
    const DominoTimer* timer_ptr = dominoTimerHeapPeek(&g_domino_timer.heap);
    if (timer_ptr != nullptr) {
        if (timer_ptr->cancelled_flag) {
            checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
            return CODE_OK;
        }
        domino_runtime_ms_t now_ms;
        code = dominoTimeGetRuntimeMs(&now_ms);
        if (code != CODE_OK) {
            goto failed;
        }
        if (timer_ptr->due_ms <= now_ms) {
            checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
            return CODE_OK;
        }
        code = getWaitDeadline(timer_ptr->due_ms - now_ms, &deadline);
        if (code != CODE_OK) {
            goto failed;
        }
        timed_wait_flag = true;
    }
    if (timed_wait_flag) {
        int result = cnd_timedwait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex, &deadline);
        // 超时属于正常返回。
        if (result != thrd_timedout) {
            checkThreadResult(result, ERR_CONDITION_WAIT, "cnd_timedwait");
        }
    } else {
        checkThreadResult(cnd_wait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex), ERR_CONDITION_WAIT, "cnd_wait");
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return CODE_OK;
failed:
    code = failTimerLocked(code);
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return code;
}

int dominoTimerThread(void* arg_ptr) {
    (void)arg_ptr;
    DOMINO_CODE stop_result = CODE_OK;
    for (;;) {
        if (isTimerStopRequested()) {
            break;
        }
        checkThreadResult(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
        // 一轮堆处理与命令处理共同构成一批；仅在批次边界等待解冻，不发送冻结回执。
        while (!isTimerStopRequested() && dominoTimeModuleIsFrozen()) {
            stop_result = dominoEngineGetLifecycleError();
            if (stop_result != CODE_OK) {
                stop_result = failTimerLocked(stop_result);
                break;
            }
            g_domino_timer.wake_pending_flag = false;
            checkThreadResult(cnd_wait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex), ERR_CONDITION_WAIT, "cnd_wait");
        }
        if (stop_result == CODE_OK) {
            stop_result = dominoEngineGetLifecycleError();
            if (stop_result != CODE_OK) {
                stop_result = failTimerLocked(stop_result);
            }
        }
        bool stop_requested_flag = isTimerStopRequested();
        // 本轮检查前清除标志；检查期间的新通知会阻止本轮睡眠。
        g_domino_timer.wake_pending_flag = false;
        if (stop_requested_flag) {
            checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
            break;
        }
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
        bool heap_batch_full_flag;
        stop_result = processHeapBatch(&heap_batch_full_flag);
        if (stop_result != CODE_OK) {
            break;
        }
        uint32_t command_count;
        stop_result = processCommandBatch(&command_count);
        if (stop_result != CODE_OK) {
            break;
        }
        // 入堆可能替换堆顶；堆处理耗尽本批预算时也需要重新检查。
        if (command_count > 0U || heap_batch_full_flag) {
            continue;
        }
        stop_result = waitForWork();
        if (stop_result != CODE_OK) {
            break;
        }
    }
    if (stop_result == CODE_OK) {
        stop_result = dominoEngineGetLifecycleError();
    }
    g_domino_timer.stop_result = stop_result;
    atomic_store_explicit(&g_domino_timer.stopped_flag, true, memory_order_release);
    return stop_result;
}
