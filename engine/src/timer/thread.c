#include <time.h>

#include "../queue/thread_message.h"
#include "../time/entry.h"
#include "internal.h"

#define DOMINO_TIMER_BATCH_SIZE 64U

/**
 * @brief 整批命令的出队与入堆共用一次请求锁，取消查找不会漏掉转移中的请求。
 * @return 实际出队数量，包括取消项；基础设施失败直接报告并终止进程。
 */
static uint32_t processCommandBatch(void) {
    uint32_t command_count = 0U;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    while (command_count < DOMINO_TIMER_BATCH_SIZE) {
        DominoTimer timer;
        DOMINO_CODE code = dominoThreadQueueConsume(&g_domino_timer.command_queue, &timer, false);
        if (code == ERR_QUEUE_EMPTY) {
            break;
        }
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "读取计时器登记命令失败");
        }
        command_count++;
        if (timer.cancelled_flag) {
            continue;
        }
        dominoTimerHeapPush(&g_domino_timer.heap, &timer);
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return command_count;
}

/**
 * @brief 每批最多 64 次回收或非阻塞投递；投递失败释放未交付载荷后报告并终止进程。
 * @return 是否耗尽本批预算，不代表确认仍有可处理的堆项。
 */
static bool processHeapBatch(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    uint32_t work_count = 0U;
    while (work_count < DOMINO_TIMER_BATCH_SIZE) {
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
        // 收件队列按值复制整条消息，计时载荷直接内嵌，不依赖堆项或当前线程的栈。
        DominoTimer timer = *timer_ptr;
        dominoTimerHeapPop(&g_domino_timer.heap);
        DominoThreadMessage message = {.type = DOMINO_THREAD_MESSAGE_TIMER, .payload.timer = timer};
        const DOMINO_CODE code = dominoThreadQueueProduce(timer.event_queue_ptr, &message);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "投递计时器到期事件失败");
        }
        work_count++;
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return work_count == DOMINO_TIMER_BATCH_SIZE;
}

static inline void getWaitDeadline(domino_runtime_ms_t delta_ms, struct timespec* deadline_out_ptr) {
    // 标准 cnd_timedwait 使用 TIME_UTC；唤醒后仍按累计未冻结的现实时间判定到期。
    if (timespec_get(deadline_out_ptr, TIME_UTC) != TIME_UTC) {
        DOMINO_EXIT_WITH_ERROR(ERR_CLOCK_READ, "计算计时器等待截止点失败: UTC 时钟采样失败");
    }
    uint32_t wait_s = delta_ms / UINT32_C(1000);
    uint32_t wait_ns = (delta_ms % UINT32_C(1000)) * UINT32_C(1000000);
    wait_ns += (uint32_t)deadline_out_ptr->tv_nsec;
    deadline_out_ptr->tv_sec += (time_t)(wait_s + wait_ns / UINT32_C(1000000000));
    deadline_out_ptr->tv_nsec = (long)(wait_ns % UINT32_C(1000000000));
}

/** @brief 持请求锁决定等待；期间收到通知则返回主循环，条件等待自动释放请求锁。 */
static void waitForWork(void) {
    struct timespec deadline = {0};
    bool timed_wait_flag = false;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    if (dominoEngineIsStopping()) {
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
        return;
    }
    if (g_domino_timer.wake_pending_flag || dominoTimeModuleIsFrozen()) {
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
        return;
    }
    const DominoTimer* timer_ptr = dominoTimerHeapPeek(&g_domino_timer.heap);
    if (timer_ptr != nullptr) {
        if (timer_ptr->cancelled_flag) {
            DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
            return;
        }
        domino_runtime_ms_t now_ms;
        dominoTimeGetRuntimeMs(&now_ms);
        if (timer_ptr->due_ms <= now_ms) {
            DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
            return;
        }
        getWaitDeadline(timer_ptr->due_ms - now_ms, &deadline);
        timed_wait_flag = true;
    }
    if (timed_wait_flag) {
        int result = cnd_timedwait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex, &deadline);
        // 超时属于正常返回。
        if (result != thrd_timedout) {
            DOMINO_REQUIRE_THREAD_SUCCESS(result, ERR_CONDITION_WAIT, "cnd_timedwait");
        }
    } else {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex), ERR_CONDITION_WAIT, "cnd_wait");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
}

int dominoTimerThread(void* arg_ptr) {
    (void)arg_ptr;
    for (;;) {
        if (dominoEngineIsStopping()) {
            break;
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
        // 一轮堆处理与命令处理共同构成一批；仅在批次边界等待解冻，不发送冻结回执。
        while (!dominoEngineIsStopping() && dominoTimeModuleIsFrozen()) {
            g_domino_timer.wake_pending_flag = false;
            DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex), ERR_CONDITION_WAIT, "cnd_wait");
        }
        // 本轮检查前清除标志；检查期间的新通知会阻止本轮睡眠。
        g_domino_timer.wake_pending_flag = false;
        if (dominoEngineIsStopping()) {
            DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
            break;
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
        bool heap_batch_full_flag = processHeapBatch();
        uint32_t command_count = processCommandBatch();
        // 入堆可能替换堆顶；堆处理耗尽本批预算时也需要重新检查。
        if (command_count > 0U || heap_batch_full_flag) {
            continue;
        }
        waitForWork();
    }
    return CODE_OK;
}
