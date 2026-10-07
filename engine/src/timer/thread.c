#include <inttypes.h>
#include <time.h>

#include "../time/entry.h"
#include "internal.h"

#define DOMINO_TIMER_BATCH_SIZE 64U

/**
 * @brief 整批命令的出队与入堆共用一次请求锁，取消查找不会漏掉转移中的请求。
 * @return 实际出队的命令数量，包括取消项；最多 64 条。后台入堆失败时立即终止程序。
 */
static uint32_t processCommandBatch(void) {
    uint32_t command_count = 0U;
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    for (; command_count < DOMINO_TIMER_BATCH_SIZE; ++command_count) {
        DominoTimer timer;
        DOMINO_CODE code = dominoThreadQueueConsume(&g_domino_timer.command_queue, &timer, false);
        if (code == ERR_QUEUE_EMPTY) {
            break;
        }
        checkQueueResult(code);
        if (timer.cancelled_flag) {
            continue;
        }
        timer.timer_id = timer.id;
        code = dominoTimerHeapPush(&g_domino_timer.heap, &timer);
        if (code != CODE_OK) {
            (void)fprintf(stderr, "domino timer: registration for queue %p, timer %" PRIu64 " failed (%d)\n", (void*)timer.event_queue_ptr,
                          timer.timer_id, code);
            abort();
        }
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    return command_count;
}

/**
 * @brief 每批最多 64 次回收或投递；后台投递失败时立即终止程序。
 * @return true 表示已耗尽本批预算，需要继续检查，不代表确认仍有可处理的堆项。
 */
static bool processHeapBatch(void) {
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    uint64_t now_ms = dominoTimeGetRuntimeMs();
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
        // 队列回写消息 ID，保留原 timer_id 供取消查找。
        DominoTimer timer = *timer_ptr;
        dominoTimerHeapPop(&g_domino_timer.heap);
        DOMINO_CODE code = dominoThreadQueueProduce(timer.event_queue_ptr, &timer, false);
        if (code != CODE_OK) {
            (void)fprintf(stderr, "domino timer: delivery to queue %p, timer %" PRIu64 " failed (%d)\n", (void*)timer.event_queue_ptr, timer.timer_id,
                          code);
            abort();
        }
        work_count++;
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
    return work_count == DOMINO_TIMER_BATCH_SIZE;
}

static inline struct timespec getWaitDeadline(uint64_t delta_ms) {
    struct timespec deadline;
    // 标准 cnd_timedwait 使用 TIME_UTC；唤醒后仍按累计未冻结的现实时间判定到期。
    if (timespec_get(&deadline, TIME_UTC) != TIME_UTC) {
        (void)fprintf(stderr, "domino timer: failed to read UTC clock\n");
        abort();
    }
    uint64_t seconds = delta_ms / UINT64_C(1000);
    uint64_t nanoseconds = (delta_ms % UINT64_C(1000)) * UINT64_C(1000000);
    nanoseconds += (uint64_t)deadline.tv_nsec;
    deadline.tv_sec += (time_t)(seconds + nanoseconds / UINT64_C(1000000000));
    deadline.tv_nsec = (long)(nanoseconds % UINT64_C(1000000000));
    return deadline;
}

/** @brief 持请求锁决定等待；期间收到通知则返回主循环，条件等待自动释放请求锁。 */
static void waitForWork(void) {
    struct timespec deadline = {0};
    bool timed_wait_flag = false;
    checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
    if (g_domino_timer.stop_requested_flag || g_domino_timer.wake_pending_flag) {
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
        return;
    }
    const DominoTimer* timer_ptr = dominoTimerHeapPeek(&g_domino_timer.heap);
    if (timer_ptr != nullptr) {
        if (timer_ptr->cancelled_flag) {
            checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
            return;
        }
        uint64_t now_ms = dominoTimeGetRuntimeMs();
        if (timer_ptr->due_ms <= now_ms) {
            checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
            return;
        }
        deadline = getWaitDeadline(timer_ptr->due_ms - now_ms);
        timed_wait_flag = true;
    }
    if (timed_wait_flag) {
        int result = cnd_timedwait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex, &deadline);
        // 超时属于正常返回。
        if (result != thrd_timedout) {
            checkThreadResult(result);
        }
    } else {
        checkThreadResult(cnd_wait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex));
    }
    checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
}

int dominoTimerThread(void* arg_ptr) {
    (void)arg_ptr;
    for (;;) {
        checkThreadResult(mtx_lock(&g_domino_timer.request_mutex));
        // 每轮头部检查停止和冻结；发现冻结后仅等待解冻或退出。
        while (!g_domino_timer.stop_requested_flag && dominoTimeModuleIsFrozen()) {
            g_domino_timer.wake_pending_flag = false;
            checkThreadResult(cnd_wait(&g_domino_timer.wake_condition, &g_domino_timer.request_mutex));
        }
        bool stop_requested_flag = g_domino_timer.stop_requested_flag;
        // 本轮检查前清除标志；检查期间的新通知会阻止本轮睡眠。
        g_domino_timer.wake_pending_flag = false;
        checkThreadResult(mtx_unlock(&g_domino_timer.request_mutex));
        if (stop_requested_flag) {
            break;
        }
        bool heap_batch_full_flag = processHeapBatch();
        uint32_t command_count = processCommandBatch();
        // 入堆可能替换堆顶；堆处理耗尽本批预算时也需要重新检查。
        if (command_count > 0U || heap_batch_full_flag) {
            continue;
        }
        waitForWork();
    }
    return 0;
}
