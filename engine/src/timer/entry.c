#include "entry.h"

#include <inttypes.h>
#include <string.h>
#include <time.h>

#include "../time/entry.h"
#include "internal.h"

DominoTimerModule g_domino_timer;

DOMINO_CODE dominoTimerModuleInit(void) {
    const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
    if (failure_code != CODE_OK) {
        return failure_code;
    }
    if (g_domino_timer.initialized_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "计时器模块重复初始化");
        return ERR_ALREADY_INITIALIZED;
    }
    memset(&g_domino_timer, 0, sizeof(g_domino_timer));
    g_domino_timer.stop_result = CODE_OK;
    atomic_init(&g_domino_timer.failure_code, CODE_OK);
    atomic_init(&g_domino_timer.stop_requested_flag, false);
    atomic_init(&g_domino_timer.stopped_flag, false);
    DOMINO_CODE code = dominoThreadQueueInit(&g_domino_timer.command_queue, sizeof(DominoTimer), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                             DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
    if (code != CODE_OK) {
        return code;
    }
    if (mtx_init(&g_domino_timer.request_mutex, mtx_plain) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "初始化计时器请求锁失败");
        return ERR_MUTEX_INIT;
    }
    if (cnd_init(&g_domino_timer.wake_condition) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "初始化计时器唤醒条件失败");
        return ERR_CONDITION_INIT;
    }
    if (thrd_create(&g_domino_timer.thread, dominoTimerThread, nullptr) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "创建计时器线程失败");
        return ERR_THREAD_CREATE;
    }
    g_domino_timer.initialized_flag = true;
    return dominoEngineGetLifecycleError();
}

/** @brief 短暂等待退出进度；信号中断只结束本次等待，其他睡眠错误直接返回。 */
static DOMINO_CODE waitForTimerExitProgress(void) {
    const struct timespec wait_duration = {.tv_sec = 0, .tv_nsec = 1000000L};
    int result = thrd_sleep(&wait_duration, nullptr);
    if (result != 0 && result != -1) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "等待计时器退出时线程休眠失败: result=%d", result);
        return ERR_THREAD_SLEEP;
    }
    return CODE_OK;
}

/** @brief 退出同步对象已不可信；标记本地首错，禁止上层失败通知再次获取该锁。 */
static DOMINO_CODE failTimerExitSync(DOMINO_CODE code, const char* operation_ptr) {
    int expected_code = CODE_OK;
    atomic_compare_exchange_strong(&g_domino_timer.failure_code, &expected_code, code);
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "计时器退出同步失败: operation=%s, code=%d", operation_ptr, code);
    return (DOMINO_CODE)atomic_load(&g_domino_timer.failure_code);
}

DOMINO_CODE dominoTimerModuleExit(void) {
    if (!g_domino_timer.initialized_flag) {
        return CODE_OK;
    }
    const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
    if (failure_code != CODE_OK) {
        dominoTimerRequestFailure(failure_code);
        return failure_code;
    }
    if (mtx_lock(&g_domino_timer.request_mutex) != thrd_success) {
        return failTimerExitSync(ERR_MUTEX_LOCK, "mtx_lock");
    }
    atomic_store_explicit(&g_domino_timer.stop_requested_flag, true, memory_order_release);
    if (cnd_signal(&g_domino_timer.wake_condition) != thrd_success) {
        return failTimerExitSync(ERR_CONDITION_SIGNAL, "cnd_signal");
    }
    if (mtx_unlock(&g_domino_timer.request_mutex) != thrd_success) {
        return failTimerExitSync(ERR_MUTEX_UNLOCK, "mtx_unlock");
    }
    while (!atomic_load_explicit(&g_domino_timer.stopped_flag, memory_order_acquire)) {
        const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
        if (failure_code != CODE_OK) {
            return failure_code;
        }
        DOMINO_CODE code = waitForTimerExitProgress();
        if (code != CODE_OK) {
            return code;
        }
    }
    DOMINO_CODE code = g_domino_timer.stop_result;
    if (code == CODE_OK) {
        code = dominoEngineGetLifecycleError();
    }
    if (code != CODE_OK) {
        return code;
    }
    if (thrd_join(g_domino_timer.thread, nullptr) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "回收计时器线程失败");
        return ERR_THREAD_JOIN;
    }
    code = dominoThreadQueueDestroy(&g_domino_timer.command_queue);
    if (code != CODE_OK) {
        return code;
    }
    dominoTimerHeapExit(&g_domino_timer.heap);
    cnd_destroy(&g_domino_timer.wake_condition);
    mtx_destroy(&g_domino_timer.request_mutex);
    g_domino_timer.initialized_flag = false;
    return dominoEngineGetLifecycleError();
}

void dominoTimerNotifyTimeChanged(void) {
    if (g_domino_timer.initialized_flag) {
        notifyTimerThread();
    }
}

void dominoTimerRequestFailure(DOMINO_CODE code) {
    if (!g_domino_timer.initialized_flag || atomic_load(&g_domino_timer.failure_code) != CODE_OK ||
        atomic_load_explicit(&g_domino_timer.stopped_flag, memory_order_acquire)) {
        return;
    }
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    (void)failTimerLocked(code);
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
}

/**
 * @brief 只校验共同登记条件，不修改请求或采样业务时刻。
 * @pre 登记入口已确认 timer_ptr 非空。
 */
static DOMINO_CODE validateTimerRequest(const DominoTimer* timer_ptr) {
    if (!g_domino_timer.initialized_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "登记计时器失败: 计时器模块未初始化");
        return ERR_NOT_INITIALIZED;
    }
    const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
    if (failure_code != CODE_OK) {
        return failure_code;
    }
    if (timer_ptr->cancelled_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "登记计时器失败: 请求已标记取消");
        return ERR_INVALID_PARAM;
    }
    if (timer_ptr->event_queue_ptr == nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "登记计时器失败: 业务队列指针为空");
        return ERR_NULL_POINTER;
    }
    if (!timer_ptr->event_queue_ptr->initialized) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "登记计时器失败: 业务队列未初始化");
        return ERR_NOT_INITIALIZED;
    }
    if (timer_ptr->event_queue_ptr->item_size != sizeof(DominoThreadMessage)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "登记计时器失败: 业务队列元素大小错误: item_size=%zu",
                          timer_ptr->event_queue_ptr->item_size);
        return ERR_INVALID_PARAM;
    }
    return CODE_OK;
}

/** @brief 按值提交已校验的请求；调用方已填写绝对截止点。 */
static DOMINO_CODE submitTimerRequest(DominoTimer* timer_ptr) {
    DOMINO_CODE code = dominoThreadQueueProduce(&g_domino_timer.command_queue, timer_ptr);
    if (code == CODE_OK) {
        timer_ptr->timer_id = timer_ptr->id;
        notifyTimerThread();
    } else {
        logTimerQueueFailure(code, "提交登记命令");
        code = dominoEngineReportFailure(code);
        dominoTimerRequestFailure(code);
    }
    return code;
}

__attribute__((hot)) DOMINO_CODE dominoTimerSchedule(DominoTimer* timer_ptr) {
    if (timer_ptr == nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "登记计时器失败: 请求指针为空");
        return ERR_NULL_POINTER;
    }
    timer_ptr->timer_id = 0U;
    DOMINO_CODE code = validateTimerRequest(timer_ptr);
    if (code != CODE_OK) {
        return code;
    }
    domino_runtime_ms_t now_ms;
    code = dominoTimeGetRuntimeMs(&now_ms);
    if (code != CODE_OK) {
        return code;
    }
    if (timer_ptr->delay_ms > UINT32_MAX - now_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR,
                          "计时器截止点超出 32 位业务毫秒范围: now_ms=%" PRIu32 ", delay_ms=%" PRIu32, now_ms, timer_ptr->delay_ms);
        return ERR_OUT_OF_RANGE;
    }
    timer_ptr->due_ms = now_ms + timer_ptr->delay_ms;
    return submitTimerRequest(timer_ptr);
}

__attribute__((hot)) DOMINO_CODE dominoTimerScheduleAt(DominoTimer* timer_ptr, domino_runtime_ms_t due_ms) {
    if (timer_ptr == nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "登记计时器失败: 请求指针为空");
        return ERR_NULL_POINTER;
    }
    timer_ptr->timer_id = 0U;
    DOMINO_CODE code = validateTimerRequest(timer_ptr);
    if (code != CODE_OK) {
        return code;
    }
    domino_runtime_ms_t now_ms;
    code = dominoTimeGetRuntimeMs(&now_ms);
    if (code != CODE_OK) {
        return code;
    }
    timer_ptr->due_ms = due_ms;
    timer_ptr->delay_ms = due_ms > now_ms ? due_ms - now_ms : 0U;
    return submitTimerRequest(timer_ptr);
}
