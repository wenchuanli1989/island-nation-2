#include "planner.h"

#include <inttypes.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "../time/entry.h"
#include "candidate.h"
#include "domino_shared_common.h"
#include "entry.h"
#include "rules.h"

/** @file planner.c
 * @brief 三类规划线程的共用循环：Snapshot → 退出／冻结检查 → 至多 high_quota 个高优先级请求 + 1 个普通请求。
 * @note 每类上下文各自拥有线程、队列、配置、批次数组和统计，循环之间不共享可变状态。
 */

typedef struct HumanPlannerContext {
    DominoThreadQueue request_queue_list[DOMINO_HUMAN_PLAN_PRIORITY_COUNT];
    DominoThreadQueue result_queue_list[DOMINO_HUMAN_PLAN_PRIORITY_COUNT];
    DominoHumanPlanRequestMessage high_batch_list[DOMINO_HUMAN_PLANNER_HIGH_QUOTA_MAX];
    DominoHumanPlannerConfig config;
    DominoHumanPlannerStats stats;
    DominoHumanPlanComputeFn compute_fn;
    thrd_t thread;
    uint8_t kind;
} HumanPlannerContext;

static HumanPlannerContext g_planner_list[DOMINO_HUMAN_PLANNER_COUNT];
static DominoThreadQueue* g_executor_inbox_ptr;

#ifdef DOMINO_PLAYGROUND_MODE
static DominoHumanPlanComputeFn g_compute_override_list[DOMINO_HUMAN_PLANNER_COUNT];
#endif

// 十二条规划队列满载时的槽位内存上限。
static_assert((sizeof(DominoHumanPlanRequestMessage) + sizeof(DominoHumanPlanResultMessage)) * DOMINO_HUMAN_PLANNER_COUNT *
                      DOMINO_HUMAN_PLAN_PRIORITY_COUNT * (uint64_t)DOMINO_THREAD_QUEUE_MAX_CAPACITY <=
                  (uint64_t)64U * 1024U * 1024U,
              "twelve planning queues exceed their 64MiB slot budget");

static const DominoHumanPlanComputeFn g_default_compute_list[DOMINO_HUMAN_PLANNER_COUNT] = {
    [DOMINO_HUMAN_PLANNER_MOTIVATION] = dominoHumanPlanMotivations,
    [DOMINO_HUMAN_PLANNER_TASK] = dominoHumanPlanTasks,
    [DOMINO_HUMAN_PLANNER_BEHAVIOR] = dominoHumanPlanBehaviors,
};

/** @brief 持读锁核对请求身份并计算：身份失效则丢弃；有效则在解锁后投递结果并唤醒执行器。 */
static void processRequest(HumanPlannerContext* context_ptr, uint8_t priority, const DominoHumanPlanRequestMessage* request_ptr) {
    const uint64_t begin_ns = dominoGetMonotonicTimeNs();
    context_ptr->stats.request_count_list[priority]++;
    DominoHumanPlanResultMessage result = {
        .request_id = request_ptr->request_id,
        .human_id = request_ptr->human_id,
        .planner_kind = context_ptr->kind,
        .priority = priority,
    };
    dominoHumanReadLock();
    const DominoHuman* human_ptr = dominoGetHumanById(request_ptr->human_id);
    const DominoHumanPlanSlot* slot_ptr = human_ptr ? &dominoHumanRuntimeOf(human_ptr)->plan_slot_list[context_ptr->kind] : nullptr;
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    if (!slot_ptr || slot_ptr->request_id != request_ptr->request_id || slot_ptr->priority != priority) {
        // 旧请求不得读取新请求的触发范围，直接丢弃。有效请求不设期限，排队再久也照常计算。
        dominoHumanReadUnlock();
        context_ptr->stats.dropped_count++;
        return;
    }
    DominoHumanPlanOutput output = {0};
    result.outcome = context_ptr->compute_fn(human_ptr, slot_ptr, now_ms, context_ptr->config.compute_budget, &output);
    dominoHumanReadUnlock();
    if (result.outcome == DOMINO_HUMAN_PLAN_OUTCOME_NONE || result.outcome >= DOMINO_HUMAN_PLAN_OUTCOME_COUNT ||
        (result.outcome == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE) != (output.candidate_ptr != nullptr)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "planner kind=%u returned an invalid outcome=%u", context_ptr->kind, result.outcome);
    }
    // 下次规划时刻只属于动机和任务规划器，且须晚于规划时刻。
    if (output.next_plan_valid_flag && (context_ptr->kind == DOMINO_HUMAN_PLANNER_BEHAVIOR || output.next_plan_at_ms <= now_ms)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "planner kind=%u returned an invalid next plan time=%" PRIu32 " now_ms=%" PRIu32, context_ptr->kind,
                               output.next_plan_at_ms, now_ms);
    }
    result.candidate_ptr = output.candidate_ptr;
    result.next_plan_at_ms = output.next_plan_at_ms;
    result.next_plan_valid_flag = output.next_plan_valid_flag;
    result.enqueued_monotonic_ns = dominoGetMonotonicTimeNs();
    const DOMINO_CODE code = dominoThreadQueueProduce(&context_ptr->result_queue_list[priority], &result, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot deliver plan result kind=%u human=%" PRIu32 " request=%" PRIu64, context_ptr->kind,
                               request_ptr->human_id, request_ptr->request_id);
    }
    // 入队成功后候选归执行器；执行器等待在收件箱上，因此唤醒收件箱。
    dominoThreadQueueWakeAll(g_executor_inbox_ptr);
    const uint64_t elapsed_ns = dominoGetMonotonicTimeNs() - begin_ns;
    context_ptr->stats.busy_ns += elapsed_ns;
    if (elapsed_ns > context_ptr->stats.max_request_ns) {
        context_ptr->stats.max_request_ns = elapsed_ns;
    }
}

static int plannerThreadMain(void* arg_ptr) {
    HumanPlannerContext* context_ptr = arg_ptr;
    DominoThreadQueue* normal_queue_ptr = &context_ptr->request_queue_list[DOMINO_HUMAN_PLAN_PRIORITY_NORMAL];
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(normal_queue_ptr, &epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot snapshot planner queue kind=%u", context_ptr->kind);
        }
        if (dominoEngineIsStopping()) {
            return 0;
        }
        const uint64_t freeze_epoch = dominoTimeModuleGetFreezeEpoch();
        if (freeze_epoch != 0U) {
            dominoTimeModuleWait(freeze_epoch);
            continue;
        }
        uint32_t high_count = 0U;
        code = dominoThreadQueueConsumeBatch(&context_ptr->request_queue_list[DOMINO_HUMAN_PLAN_PRIORITY_HIGH], context_ptr->high_batch_list,
                                             context_ptr->config.high_quota, &high_count, false);
        if (code != CODE_OK && code != ERR_QUEUE_EMPTY) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot consume high plan requests kind=%u", context_ptr->kind);
        }
        for (uint32_t index = 0U; index < high_count; index++) {
            processRequest(context_ptr, DOMINO_HUMAN_PLAN_PRIORITY_HIGH, &context_ptr->high_batch_list[index]);
        }
        DominoHumanPlanRequestMessage normal_request;
        code = dominoThreadQueueConsume(normal_queue_ptr, &normal_request, false);
        if (code != CODE_OK && code != ERR_QUEUE_EMPTY) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot consume normal plan request kind=%u", context_ptr->kind);
        }
        const uint32_t batch_size = high_count + (code == CODE_OK ? 1U : 0U);
        if (code == CODE_OK) {
            processRequest(context_ptr, DOMINO_HUMAN_PLAN_PRIORITY_NORMAL, &normal_request);
        }
        if (batch_size != 0U) {
            context_ptr->stats.batch_count++;
            if (batch_size > context_ptr->stats.max_batch_size) {
                context_ptr->stats.max_batch_size = batch_size;
            }
            continue;
        }
        code = dominoThreadQueueWait(normal_queue_ptr, epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot wait for plan requests kind=%u", context_ptr->kind);
        }
    }
}

void dominoHumanPlannerInit(DominoThreadQueue* executor_inbox_ptr) {
    g_executor_inbox_ptr = executor_inbox_ptr;
    for (uint8_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        HumanPlannerContext* context_ptr = &g_planner_list[kind];
        context_ptr->kind = kind;
        context_ptr->config = g_domino_human_ai_config.planner_list[kind];
        context_ptr->compute_fn = g_default_compute_list[kind];
#ifdef DOMINO_PLAYGROUND_MODE
        if (g_compute_override_list[kind] != nullptr) {
            context_ptr->compute_fn = g_compute_override_list[kind];
        }
#endif
        for (uint8_t priority = 0U; priority < DOMINO_HUMAN_PLAN_PRIORITY_COUNT; priority++) {
            dominoThreadQueueInit(&context_ptr->request_queue_list[priority], sizeof(DominoHumanPlanRequestMessage),
                                  DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, DOMINO_THREAD_QUEUE_MAX_CAPACITY);
            dominoThreadQueueInit(&context_ptr->result_queue_list[priority], sizeof(DominoHumanPlanResultMessage),
                                  DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, DOMINO_THREAD_QUEUE_MAX_CAPACITY);
        }
    }
}

void dominoHumanPlannerStart(void) {
    for (uint8_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_planner_list[kind].thread, plannerThreadMain, &g_planner_list[kind]), ERR_THREAD_CREATE,
                                      "cannot create human planner thread");
    }
}

void dominoHumanPlannerNotifyTimeChanged(void) {
    for (uint8_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        dominoThreadQueueWakeAll(&g_planner_list[kind].request_queue_list[DOMINO_HUMAN_PLAN_PRIORITY_NORMAL]);
    }
}

void dominoHumanPlannerStop(void) {
    for (uint8_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_planner_list[kind].thread, nullptr), ERR_THREAD_JOIN, "cannot join human planner thread");
    }
}

void dominoHumanPlannerDestroy(void) {
    // 留队请求和结果只属于本会话，重新加载后由业务事实重新规划；这里只释放结果中的候选。
    for (uint8_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        HumanPlannerContext* context_ptr = &g_planner_list[kind];
        for (uint8_t priority = 0U; priority < DOMINO_HUMAN_PLAN_PRIORITY_COUNT; priority++) {
            DominoHumanPlanResultMessage result;
            DOMINO_CODE code;
            while ((code = dominoThreadQueueConsume(&context_ptr->result_queue_list[priority], &result, false)) == CODE_OK) {
                dominoHumanCandidateFree(result.candidate_ptr);
            }
            if (code != ERR_QUEUE_EMPTY) {
                DOMINO_EXIT_WITH_ERROR(code, "cannot drain plan results kind=%u", kind);
            }
            dominoThreadQueueDestroy(&context_ptr->request_queue_list[priority]);
            dominoThreadQueueDestroy(&context_ptr->result_queue_list[priority]);
        }
    }
    memset(g_planner_list, 0, sizeof(g_planner_list));
    g_executor_inbox_ptr = nullptr;
}

uint64_t dominoHumanPlannerPostRequest(uint8_t planner_kind, uint8_t priority, domino_human_id_t human_id) {
    HumanPlannerContext* context_ptr = &g_planner_list[planner_kind];
    DominoHumanPlanRequestMessage request = {.human_id = human_id};
    const DOMINO_CODE code = dominoThreadQueueProduce(&context_ptr->request_queue_list[priority], &request, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot post plan request kind=%u human=%" PRIu32, planner_kind, human_id);
    }
    if (priority == DOMINO_HUMAN_PLAN_PRIORITY_HIGH) {
        // 规划线程只等待在普通请求队列上，高优先级入队后须唤醒它。
        dominoThreadQueueWakeAll(&context_ptr->request_queue_list[DOMINO_HUMAN_PLAN_PRIORITY_NORMAL]);
    }
    return request.request_id;
}

DominoThreadQueue* dominoHumanPlannerGetResultQueue(uint8_t planner_kind, uint8_t priority) {
    return &g_planner_list[planner_kind].result_queue_list[priority];
}

DominoHumanPlannerStats dominoHumanPlannerGetStats(uint8_t planner_kind) {
    return g_planner_list[planner_kind].stats;
}

#ifdef DOMINO_PLAYGROUND_MODE
void dominoHumanPlannerSetComputeOverride(uint8_t planner_kind, DominoHumanPlanComputeFn compute_fn) {
    g_compute_override_list[planner_kind] = compute_fn;
}
#endif
