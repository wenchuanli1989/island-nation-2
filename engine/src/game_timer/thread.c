#include <time.h>

#include "../queue/message.h"
#include "../time/entry.h"
#include "entry.h"
#include "internal.h"

#define DOMINO_GAME_TIMER_BATCH_MAX 64U

/** @brief 把至多一批命令移入堆，已取消的直接丢弃；全程持请求锁，取消查找不会漏掉迁移中的请求。
 * @return 出队的命令数（含丢弃的）。返回时仍持锁，调用方可据此决定是否等待。
 */
static uint32_t processCommandBatchLocked(void) {
    uint32_t command_count = 0U;
    while (command_count < DOMINO_GAME_TIMER_BATCH_MAX) {
        DominoGameTimerRequestMessage request;
        DOMINO_CODE code = dominoThreadQueueConsume(&g_domino_game_timer.command_queue, &request, false);
        if (code == ERR_QUEUE_EMPTY) {
            break;
        }
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "读取计时器登记命令失败");
        }
        command_count++;
        if (request.cancelled_flag) {
            continue;
        }
        dominoGameTimerHeapPush(&g_domino_game_timer.heap, &request);
    }
    return command_count;
}

/** @brief 至多一批：回收已取消的堆顶，把已到期的请求投递到目标收件队列。
 * @return 是否用满本批额度（用满时可能还有到期请求）。
 */
static bool processHeapBatch(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_game_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    uint32_t work_count = 0U;
    while (work_count < DOMINO_GAME_TIMER_BATCH_MAX) {
        const DominoGameTimerRequestMessage* request_ptr = dominoGameTimerHeapPeek(&g_domino_game_timer.heap);
        if (request_ptr == nullptr) {
            break;
        }
        // 已取消的堆顶立即回收，不等原截止点。
        if (request_ptr->cancelled_flag) {
            dominoGameTimerHeapPop(&g_domino_game_timer.heap);
            work_count++;
            continue;
        }
        if (request_ptr->due_ms > now_ms) {
            break;
        }
        // 取消检查、出堆和投递都在请求锁内完成，Cancel 不会插在中间。
        DominoGameTimerRequestMessage request = *request_ptr;
        dominoGameTimerHeapPop(&g_domino_game_timer.heap);
        DominoHumanInboxMessage message = {.game_timer = request};
        const DOMINO_CODE code = dominoThreadQueueProduce(request.event_queue_ptr, &message, false);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "投递计时器到期事件失败");
        }
        work_count++;
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_game_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return work_count == DOMINO_GAME_TIMER_BATCH_MAX;
}

/** @brief cnd_timedwait 需要 TIME_UTC 绝对时刻；醒来后仍按业务时间重新判定是否到期。 */
static inline void calcWaitDeadline(domino_runtime_ms_t delta_ms, struct timespec* deadline_out_ptr) {
    if (timespec_get(deadline_out_ptr, TIME_UTC) != TIME_UTC) {
        DOMINO_EXIT_WITH_ERROR(ERR_CLOCK_READ, "计算计时器等待截止点失败: UTC 时钟采样失败");
    }
    uint32_t wait_s = delta_ms / UINT32_C(1000);
    uint32_t wait_ns = (delta_ms % UINT32_C(1000)) * UINT32_C(1000000);
    wait_ns += (uint32_t)deadline_out_ptr->tv_nsec;
    deadline_out_ptr->tv_sec += (time_t)(wait_s + wait_ns / UINT32_C(1000000000));
    deadline_out_ptr->tv_nsec = (long)(wait_ns % UINT32_C(1000000000));
}

/** @brief 复查退出、冻结和堆顶后进入等待：有未来截止点时限时等待，否则无限等待。
 * @pre 从确认命令队列为空起一直持有请求锁；登记方须持同一把锁才能发信号，所以不会漏掉通知（宪法 2.3）。
 */
static void waitForWorkLocked(void) {
    struct timespec deadline = {0};
    bool timed_wait_flag = false;
    if (dominoEngineIsStopping()) {
        return;
    }
    if (dominoTimeModuleIsFrozen()) {
        return;
    }
    const DominoGameTimerRequestMessage* request_ptr = dominoGameTimerHeapPeek(&g_domino_game_timer.heap);
    if (request_ptr != nullptr) {
        if (request_ptr->cancelled_flag) {
            return;
        }
        domino_runtime_ms_t now_ms;
        dominoTimeGetRuntimeMs(&now_ms);
        if (request_ptr->due_ms <= now_ms) {
            return;
        }
        calcWaitDeadline(request_ptr->due_ms - now_ms, &deadline);
        timed_wait_flag = true;
    }
    if (timed_wait_flag) {
        int result = cnd_timedwait(&g_domino_game_timer.wake_condition, &g_domino_game_timer.request_mutex, &deadline);
        if (result != thrd_timedout) {
            DOMINO_REQUIRE_THREAD_SUCCESS(result, ERR_CONDITION_WAIT, "cnd_timedwait");
        }
    } else {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&g_domino_game_timer.wake_condition, &g_domino_game_timer.request_mutex), ERR_CONDITION_WAIT,
                                      "cnd_wait");
    }
}

int dominoGameTimerThreadMain(void* arg_ptr) {
    (void)arg_ptr;
    for (;;) {
        if (dominoEngineIsStopping()) {
            break;
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_game_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
        // 一次堆处理加一次命令迁移构成一批；只在批次边界等待解冻。
        while (!dominoEngineIsStopping() && dominoTimeModuleIsFrozen()) {
            DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&g_domino_game_timer.wake_condition, &g_domino_game_timer.request_mutex), ERR_CONDITION_WAIT,
                                          "cnd_wait");
        }
        if (dominoEngineIsStopping()) {
            DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_game_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
            break;
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_game_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
        bool heap_batch_full_flag = processHeapBatch();
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_game_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
        uint32_t command_count = processCommandBatchLocked();
        // 新入堆的请求可能替换堆顶，堆处理用满额度时也可能还有到期请求：两种情况都立即开始下一批。
        if (command_count == 0U && !heap_batch_full_flag) {
            waitForWorkLocked();
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_game_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    }
    return CODE_OK;
}
