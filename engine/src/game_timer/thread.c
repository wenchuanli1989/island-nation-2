#include <time.h>

#include "../queue/thread_message.h"
#include "../time/entry.h"
#include "internal.h"

#define DOMINO_GAME_TIMER_BATCH_SIZE 64U

/**
 * @brief 整批命令的出队与入堆共用一次请求锁，取消查找不会漏掉转移中的请求。
 * @pre 调用方持有 request_mutex；函数返回时仍持锁，可继续决定是否等待。
 * @return 实际出队数量，包括取消项；基础设施失败直接报告并终止进程。
 */
static uint32_t processCommandBatchLocked(void) {
    uint32_t command_count = 0U;
    while (command_count < DOMINO_GAME_TIMER_BATCH_SIZE) {
        DominoGameTimer game_timer;
        DOMINO_CODE code = dominoThreadQueueConsume(&g_domino_game_timer.command_queue, &game_timer, false);
        if (code == ERR_QUEUE_EMPTY) {
            break;
        }
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "读取计时器登记命令失败");
        }
        command_count++;
        if (game_timer.cancelled_flag) {
            continue;
        }
        dominoGameTimerHeapPush(&g_domino_game_timer.heap, &game_timer);
    }
    return command_count;
}

/**
 * @brief 每批最多 64 次回收或非阻塞投递；投递失败释放未交付载荷后报告并终止进程。
 * @return 是否耗尽本批预算，不代表确认仍有可处理的堆项。
 */
static bool processHeapBatch(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_game_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    uint32_t work_count = 0U;
    while (work_count < DOMINO_GAME_TIMER_BATCH_SIZE) {
        const DominoGameTimer* game_timer_ptr = dominoGameTimerHeapPeek(&g_domino_game_timer.heap);
        if (game_timer_ptr == nullptr) {
            break;
        }
        // 已取消堆顶立即回收；不等待原截止点，也不占用目标事件队列。
        if (game_timer_ptr->cancelled_flag) {
            dominoGameTimerHeapPop(&g_domino_game_timer.heap);
            work_count++;
            continue;
        }
        if (game_timer_ptr->due_ms > now_ms) {
            break;
        }
        // 取消检查、堆顶移除和事件入队共用请求锁，Cancel 无法在迁移中插入。
        // 收件队列按值复制整条消息，计时载荷直接内嵌，不依赖堆项或当前线程的栈。
        DominoGameTimer game_timer = *game_timer_ptr;
        dominoGameTimerHeapPop(&g_domino_game_timer.heap);
        DominoThreadMessage message = {.type = DOMINO_THREAD_MESSAGE_GAME_TIMER, .payload.game_timer = game_timer};
        const DOMINO_CODE code = dominoThreadQueueProduce(game_timer.event_queue_ptr, &message, false);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "投递计时器到期事件失败");
        }
        work_count++;
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_game_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    return work_count == DOMINO_GAME_TIMER_BATCH_SIZE;
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

/**
 * @brief 复用命令出队的空结果决定等待；条件等待原子释放请求锁，返回时重新持锁。
 * @pre 本轮未处理命令且堆处理未耗尽预算；从命令队列判空前起，调用方持续持有 request_mutex。
 * @note 判空后仍可有新命令入队，但提交方须取得请求锁才能通知，因此不会在进入等待前漏掉信号。
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
    const DominoGameTimer* game_timer_ptr = dominoGameTimerHeapPeek(&g_domino_game_timer.heap);
    if (game_timer_ptr != nullptr) {
        if (game_timer_ptr->cancelled_flag) {
            return;
        }
        domino_runtime_ms_t now_ms;
        dominoTimeGetRuntimeMs(&now_ms);
        if (game_timer_ptr->due_ms <= now_ms) {
            return;
        }
        getWaitDeadline(game_timer_ptr->due_ms - now_ms, &deadline);
        timed_wait_flag = true;
    }
    if (timed_wait_flag) {
        int result = cnd_timedwait(&g_domino_game_timer.wake_condition, &g_domino_game_timer.request_mutex, &deadline);
        // 超时属于正常返回。
        if (result != thrd_timedout) {
            DOMINO_REQUIRE_THREAD_SUCCESS(result, ERR_CONDITION_WAIT, "cnd_timedwait");
        }
    } else {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&g_domino_game_timer.wake_condition, &g_domino_game_timer.request_mutex), ERR_CONDITION_WAIT,
                                      "cnd_wait");
    }
}

int dominoGameTimerThread(void* arg_ptr) {
    (void)arg_ptr;
    for (;;) {
        if (dominoEngineIsStopping()) {
            break;
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_game_timer.request_mutex), ERR_MUTEX_LOCK, "mtx_lock");
        // 一轮堆处理与命令处理共同构成一批；仅在批次边界等待解冻，不发送冻结回执。
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
        // 入堆可能替换堆顶；堆处理耗尽本批预算时也需要重新检查。
        if (command_count == 0U && !heap_batch_full_flag) {
            // 复用首次出队的空结果，直到条件等待原子释放请求锁前不留通知空隙。
            waitForWorkLocked();
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_game_timer.request_mutex), ERR_MUTEX_UNLOCK, "mtx_unlock");
    }
    return CODE_OK;
}
