#include "playground.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "../game_timer/entry.h"
#include "../host/entry.h"
#include "../logger/entry.h"
#include "../time/entry.h"
#include "advance.h"
#include "behavior/entry.h"
#include "candidate.h"
#include "domino_engine.h"
#include "domino_shared_common.h"
#include "entry.h"
#include "executor.h"
#include "motivation/entry.h"
#include "planner.h"
#include "rules.h"
#include "state.h"
#include "store.h"

/* ================================================================================================================ */
/* 检查与夹具                                                                                                         */
/* ================================================================================================================ */

static int g_failure_count;
static int g_check_count;
static const char* g_case_name = "";

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        g_check_count++;                                                                      \
        if (!(condition)) {                                                                   \
            g_failure_count++;                                                                \
            DOMINO_PRINT_MESSAGE("FAIL [%s] line %d: %s", g_case_name, __LINE__, #condition); \
        }                                                                                     \
    } while (0)

static void setNowMs(domino_runtime_ms_t now_ms) {
    dominoSetRuntimeDateTimeBase((uint64_t)now_ms * 1000000U);
}

static domino_runtime_ms_t nowMs(void) {
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    return now_ms;
}

/** @brief 串行夹具：logger、冻结时钟、human 和 game_timer（无线程），按给定精力建立人物后完成 InitAfter。 */
static void fixtureBegin(const char* case_name, uint32_t human_count, const uint8_t* energy_list) {
    g_case_name = case_name;
    dominoLoggerModuleInit();
    dominoLoggerModuleStart();
    dominoTimeModuleInit();
    setNowMs(0U);
    dominoHumanModuleInit();
    dominoGameTimerModuleInit();
    for (uint32_t index = 0U; index < human_count; index++) {
        const domino_human_id_t human_id = index + 1U;
        DominoHuman human = {.human_id = human_id};
        DominoHumanData data = {
            .human_id = human_id,
            .energy = energy_list[index],
            .food = 100U,
            .water = 100U,
            .hygiene = 100U,
            .recreation = 100U,
            .social = 100U,
        };
        int absent_flag;
        const khint_t map_index = dominoHumanIdMap_put(dominoHumanIdMap, human_id, &absent_flag);
        kh_val(dominoHumanIdMap, map_index) = index;
        kv_push(DominoHuman, domino_all_human_list, human);
        kv_push(DominoHumanData, domino_all_human_data_list, data);
    }
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        kv_A(domino_all_human_list, index).data = &kv_A(domino_all_human_data_list, index);
    }
    dominoHumanModuleInitAfter();
}

static void fixtureEnd(void) {
    dominoGameTimerModuleDestroy();
    dominoHumanModuleDestroy();
    atomic_store(&g_domino_global_increment_id.motivation_increment_id, 0U);
    atomic_store(&g_domino_global_increment_id.task_increment_id, 0U);
    atomic_store(&g_domino_global_increment_id.behavior_increment_id, 0U);
    dominoTimeModuleDestroy();
    dominoLoggerModuleStop();
    dominoLoggerModuleDestroy();
}

static DominoHuman* human(domino_human_id_t human_id) {
    return dominoGetHumanByID(human_id);
}

static DominoHumanRuntime* runtimeOf(domino_human_id_t human_id) {
    return dominoHumanRuntimeOf(human(human_id));
}

static DominoMotivation* restoreMotivation(domino_human_id_t human_id) {
    return dominoHumanFindActiveMotivation(human(human_id)->data, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
}

static DominoMotivation* contactMotivation(domino_human_id_t human_id) {
    return dominoHumanFindActiveMotivation(human(human_id)->data, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT);
}

/** @brief 运行任务优先，否则待执行队首。 */
static DominoTask* activeOrHead(domino_human_id_t human_id) {
    const DominoHumanData* data_ptr = human(human_id)->data;
    if (data_ptr->active_task_id != 0U) {
        return dominoGetTaskByID(data_ptr->active_task_id);
    }
    return data_ptr->task_queue_count != 0U ? dominoGetTaskByID(data_ptr->task_queue_list[0]) : nullptr;
}

/** @brief 运行任务在某通道的当前行为；没有返回空指针。 */
static DominoBehavior* channelBehavior(domino_human_id_t human_id, uint8_t channel) {
    const DominoTask* task_ptr = dominoHumanActiveTask(human(human_id)->data);
    const domino_behavior_id_t behavior_id = task_ptr != nullptr ? task_ptr->current_behavior_id_list[channel] : 0U;
    return behavior_id != 0U ? dominoGetBehaviorByID(behavior_id) : nullptr;
}

static const DominoHumanPlanComputeFn g_policy_list[DOMINO_HUMAN_PLANNER_COUNT] = {
    dominoHumanPlanMotivations,
    dominoHumanPlanTasks,
    dominoHumanPlanBehaviors,
};

/** @brief 模拟规划线程处理人物当前请求：按槽位资格调用策略，再经执行器的复核与提交路径处理结果及下次规划时刻。 */
static uint8_t serveRequest(domino_human_id_t human_id, uint8_t planner_kind, DominoHumanPlanComputeFn compute_fn) {
    const DominoHumanPlanSlot slot = runtimeOf(human_id)->slot_list[planner_kind];
    if (slot.request_id == 0U) {
        return DOMINO_HUMAN_PLAN_OUTCOME_NONE;
    }
    const domino_runtime_ms_t now_ms = nowMs();
    DominoHumanPlanOutput output = {0};
    const uint8_t outcome = (compute_fn ? compute_fn : g_policy_list[planner_kind])(human(human_id), &slot, now_ms, 512U, &output);
    if (output.next_plan_valid_flag) {
        /* 与规划线程相同的契约：只有动机和任务给出时刻，且须晚于规划时刻。 */
        CHECK(planner_kind != DOMINO_HUMAN_PLANNER_BEHAVIOR && output.next_plan_at_ms > now_ms);
    }
    DominoHumanPlanResultMessage result = {
        .request_id = slot.request_id,
        .enqueued_mono_ns = dominoMonotonicTimeNs(),
        .candidate_ptr = output.candidate_ptr,
        .human_id = human_id,
        .next_plan_at_ms = output.next_plan_at_ms,
        .planner_kind = planner_kind,
        .priority = slot.priority,
        .outcome = outcome,
        .next_plan_valid_flag = output.next_plan_valid_flag,
    };
    dominoHumanExecutionProcessResultForTest(planner_kind, slot.priority, &result);
    return outcome;
}

/** @brief 依次服务三类当前请求，直到没有新请求。 */
static void pumpRequests(domino_human_id_t human_id) {
    for (uint32_t round = 0U; round < 12U; round++) {
        bool served_flag = false;
        for (uint8_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
            if (runtimeOf(human_id)->slot_list[kind].request_id != 0U) {
                serveRequest(human_id, kind, nullptr);
                served_flag = true;
            }
        }
        if (!served_flag) {
            return;
        }
    }
}

/** @brief 模拟计时通知送达：与执行器消费匹配通知一样清除当前登记。 */
static void deliverWakeup(domino_human_id_t human_id) {
    DominoHumanRuntime* runtime_ptr = runtimeOf(human_id);
    runtime_ptr->wakeup_game_timer_id = 0U;
    runtime_ptr->wakeup_due_ms = 0U;
}

/** @brief 把时钟拨到人物当前唤醒时刻，模拟通知送达并推进；返回是否存在唤醒。 */
static bool advanceToWakeup(domino_human_id_t human_id) {
    const DominoHumanRuntime* runtime_ptr = runtimeOf(human_id);
    if (runtime_ptr->wakeup_game_timer_id == 0U) {
        return false;
    }
    setNowMs(runtime_ptr->wakeup_due_ms);
    deliverWakeup(human_id);
    dominoHumanExecutionAdvanceForTest(human_id);
    pumpRequests(human_id);
    return true;
}

/** @brief 把时钟拨到给定时刻并推进人物（模拟到期通知或待办）。 */
static void advanceAt(domino_human_id_t human_id, domino_runtime_ms_t now_ms) {
    setNowMs(now_ms);
    deliverWakeup(human_id);
    dominoHumanExecutionAdvanceForTest(human_id);
}

/** @brief 测试验收专用：构造与 dominoHumanSubmitEnergyForTest 相同的精力输入并串行处理。 */
static void submitEnergyForTest(domino_human_id_t human_id, uint8_t energy) {
    const DominoHumanExecutionMessage message = {
        .type = DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_ENERGY,
        .payload.test_energy = {.human_id = human_id, .energy = energy},
    };
    dominoHumanExecutionProcessMessageForTest(&message);
}

/** @brief 测试验收专用：构造与 dominoHumanSubmitSleepConditionForTest 相同的睡眠条件输入并串行处理。 */
static void submitConditionForTest(domino_human_id_t human_id, bool forbidden_flag) {
    const DominoHumanExecutionMessage message = {
        .type = DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_SLEEP_CONDITION,
        .payload.test_sleep_condition = {.human_id = human_id, .forbidden_flag = forbidden_flag},
    };
    dominoHumanExecutionProcessMessageForTest(&message);
}

/** @brief 测试验收专用：构造与 dominoHumanSubmitStimulusForTest 相同的外部信息输入（来源与类别同次写入）并串行处理。 */
static void submitStimulusForTest(domino_human_id_t human_id, uint8_t stimulus_kind, uint32_t source_id) {
    const DominoHumanExecutionMessage message = {
        .type = DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_STIMULUS,
        .payload.test_stimulus = {.human_id = human_id, .source_id = source_id, .stimulus_kind = stimulus_kind},
    };
    dominoHumanExecutionProcessMessageForTest(&message);
}

/** @brief 受控夹具：直接建立有效动机（可信内部场景，不经过客户端）。 */
static DominoMotivation* addMotivation(domino_human_id_t human_id, uint8_t type) {
    const DominoMotivation init = {.type = type, .status = DOMINO_MOTIVATION_STATUS_ACTIVE};
    return dominoHumanStoreAddMotivation(human(human_id)->data, &init);
}

/** @brief 受控夹具：建立已启动的运行任务（不带行为）。返回 task_id。 */
static domino_task_id_t addRunningTask(domino_human_id_t human_id, domino_motivation_id_t motivation_id, uint8_t task_type, uint8_t target_value) {
    DominoHumanData* data_ptr = human(human_id)->data;
    const DominoTask init = {
        .motivation_id = motivation_id,
        .work_source_id = motivation_id,
        .work_round = data_ptr->task_count + 1U,
        .type = task_type,
        .work_source_kind = DOMINO_TASK_SOURCE_MOTIVATION,
        .target_value = target_value,
    };
    DominoTask* task_ptr = dominoHumanStoreAddTask(data_ptr, &init);
    task_ptr->exec_status = DOMINO_TASK_EXEC_STATUS_OPEN;
    task_ptr->started_at_ms = data_ptr->settled_at_ms;
    data_ptr->active_task_id = task_ptr->task_id;
    return task_ptr->task_id;
}

/** @brief 受控夹具：为任务在某通道建立 ACTIVE 行为，分配本任务内的步骤序号。返回 behavior_id。 */
static domino_behavior_id_t addRunningBehavior(domino_task_id_t task_id, uint8_t channel, uint8_t behavior_type, domino_runtime_ms_t target_ms,
                                               uint32_t param, uint8_t flags) {
    DominoTask* task_ptr = dominoGetTaskByID(task_id);
    const DominoBehavior behavior = {
        .step_seq = ++task_ptr->last_step_seq,
        .started_at_ms = dominoGetHumanByID(task_ptr->owner_human_id)->data->settled_at_ms,
        .target_effective_ms = target_ms,
        .param = param,
        .type = behavior_type,
        .status = DOMINO_BEHAVIOR_STATUS_ACTIVE,
        .channel_index = channel,
        .flags = flags,
    };
    const domino_behavior_id_t behavior_id = dominoHumanStoreAddBehavior(task_ptr, &behavior)->behavior_id;
    dominoGetTaskByID(task_id)->current_behavior_id_list[channel] = behavior_id;
    return behavior_id;
}

/** @brief 受控夹具：让行为依赖另一个正在运行的行为。 */
static void addBehaviorDependency(domino_behavior_id_t behavior_id, domino_behavior_id_t source_id, uint8_t kind) {
    DominoBehavior* behavior_ptr = dominoGetBehaviorByID(behavior_id);
    for (uint8_t index = 0U; index < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; index++) {
        if (behavior_ptr->dependency_list[index].kind == DOMINO_BEHAVIOR_DEPENDENCY_NONE) {
            behavior_ptr->dependency_list[index] = (DominoBehaviorDependency){.step_seq = dominoGetBehaviorByID(source_id)->step_seq, .kind = kind};
            return;
        }
    }
    CHECK(false);
}

static void setDeadline(domino_task_id_t task_id, domino_runtime_ms_t deadline_ms, bool inclusive_flag) {
    DominoTask* task_ptr = dominoGetTaskByID(task_id);
    task_ptr->deadline_ms = deadline_ms;
    task_ptr->flags |= (uint8_t)(DOMINO_TASK_FLAG_DEADLINE_VALID | (inclusive_flag ? DOMINO_TASK_FLAG_DEADLINE_INCLUSIVE : 0U));
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 受控候选                                                                                                           */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 受控规划结果：一次性交出预先设定的结果分类、候选和下次规划时刻。 */
typedef struct ControlledPlan {
    void* candidate_ptr;
    domino_runtime_ms_t next_plan_at_ms;
    uint8_t outcome;
    bool next_plan_valid_flag;
} ControlledPlan;

static ControlledPlan g_controlled_plan;

/** @brief 受控规划计算：直接交出预先设定的结果，候选所有权随输出转交；候选非空时结果为 CANDIDATE。 */
static uint8_t controlledCompute(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms, uint32_t budget,
                                 DominoHumanPlanOutput* output_ptr) {
    (void)human_ptr;
    (void)slot_ptr;
    (void)now_ms;
    (void)budget;
    const ControlledPlan plan = g_controlled_plan;
    g_controlled_plan = (ControlledPlan){0};
    output_ptr->candidate_ptr = plan.candidate_ptr;
    output_ptr->next_plan_at_ms = plan.next_plan_at_ms;
    output_ptr->next_plan_valid_flag = plan.next_plan_valid_flag;
    return plan.candidate_ptr != nullptr ? DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE : plan.outcome;
}

/** @brief 以受控结果服务人物当前请求；返回候选是否被采用。 */
static bool serveControlled(domino_human_id_t human_id, uint8_t planner_kind, uint8_t outcome, void* candidate_ptr, bool next_valid_flag,
                            domino_runtime_ms_t next_at_ms) {
    g_controlled_plan =
        (ControlledPlan){.candidate_ptr = candidate_ptr, .next_plan_at_ms = next_at_ms, .outcome = outcome, .next_plan_valid_flag = next_valid_flag};
    const uint64_t accepted_before = dominoHumanExecutionGetStats().accepted_candidate_count;
    serveRequest(human_id, planner_kind, controlledCompute);
    return dominoHumanExecutionGetStats().accepted_candidate_count > accepted_before;
}

/** @brief 为人物发布一次受控规划请求资格（不经过调度条件），覆盖该类原有请求。 */
static void issueSlot(domino_human_id_t human_id, uint8_t planner_kind, uint8_t scope_kind, uint32_t scope_id) {
    static uint64_t request_seq = 1000U;
    runtimeOf(human_id)->slot_list[planner_kind] = (DominoHumanPlanSlot){
        .request_id = ++request_seq,
        .scope_id = scope_id,
        .priority = DOMINO_HUMAN_PLAN_PRIORITY_NORMAL,
        .scope_kind = scope_kind,
    };
}

static void* buildMotivationCandidate(uint8_t entry_count, uint8_t required_count, const DominoMotivationCandidateEntry* entry_list) {
    void* candidate_ptr = dominoHumanCandidateAlloc(DOMINO_HUMAN_PLANNER_MOTIVATION, entry_count, required_count, 0U);
    memcpy(dominoHumanCandidateEntries(candidate_ptr), entry_list, sizeof(DominoMotivationCandidateEntry) * entry_count);
    return candidate_ptr;
}

static void* buildTaskCandidate(uint8_t entry_count, uint8_t required_count, const DominoTaskCandidateEntry* entry_list) {
    void* candidate_ptr = dominoHumanCandidateAlloc(DOMINO_HUMAN_PLANNER_TASK, entry_count, required_count, 0U);
    memcpy(dominoHumanCandidateEntries(candidate_ptr), entry_list, sizeof(DominoTaskCandidateEntry) * entry_count);
    return candidate_ptr;
}

static void* buildBehaviorCandidate(uint8_t entry_count, uint8_t required_count, const DominoBehaviorCandidateEntry* entry_list, uint16_t step_count,
                                    const DominoBehaviorPlanItem* step_list) {
    void* candidate_ptr = dominoHumanCandidateAlloc(DOMINO_HUMAN_PLANNER_BEHAVIOR, entry_count, required_count, step_count);
    memcpy(dominoHumanCandidateEntries(candidate_ptr), entry_list, sizeof(DominoBehaviorCandidateEntry) * entry_count);
    memcpy(dominoHumanCandidateSteps(candidate_ptr), step_list, sizeof(DominoBehaviorPlanItem) * step_count);
    return candidate_ptr;
}

/** @brief 发布一次受控规划请求资格，再交给执行器复核预先构造的候选（结果不给下次规划时刻）；返回是否被采用。 */
static bool injectCandidate(domino_human_id_t human_id, uint8_t planner_kind, uint8_t scope_kind, uint32_t scope_id, void* candidate_ptr) {
    issueSlot(human_id, planner_kind, scope_kind, scope_id);
    return serveControlled(human_id, planner_kind, DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE, candidate_ptr, false, 0U);
}

/** @brief 以所属动机为来源的通话任务建议，追加到待执行队列末尾。 */
static DominoTaskCandidateEntry phoneEntry(domino_motivation_id_t motivation_id, uint32_t work_round) {
    return (DominoTaskCandidateEntry){
        .motivation_id = motivation_id,
        .work_source_id = motivation_id,
        .work_round = work_round,
        .op = DOMINO_TASK_CANDIDATE_CREATE,
        .type = DOMINO_TASK_TYPE_PHONE_CALL,
        .insert_index = DOMINO_PLAN_CANDIDATE_INSERT_APPEND,
        .work_source_kind = DOMINO_TASK_SOURCE_MOTIVATION,
    };
}

/** @brief 恢复精力动机的睡眠任务建议：读取时轮次 expected_round，工作轮次 work_round。 */
static DominoTaskCandidateEntry sleepEntry(domino_motivation_id_t motivation_id, uint32_t expected_round, uint32_t work_round) {
    return (DominoTaskCandidateEntry){
        .motivation_id = motivation_id,
        .work_source_id = motivation_id,
        .work_round = work_round,
        .expected_motivation_round = expected_round,
        .op = DOMINO_TASK_CANDIDATE_CREATE,
        .type = DOMINO_TASK_TYPE_SLEEP,
        .insert_index = DOMINO_PLAN_CANDIDATE_INSERT_APPEND,
        .target_value = 80U,
        .work_source_kind = DOMINO_TASK_SOURCE_MOTIVATION,
    };
}

/** @brief 一个只含单步骤的行为条目；依赖通过返回的步骤副本补充。 */
static DominoBehaviorCandidateEntry behaviorEntry(const DominoTask* task_ptr, uint8_t op, uint16_t step_count) {
    DominoBehaviorCandidateEntry entry = {
        .expected_plan_version = task_ptr->plan_version,
        .task_id = task_ptr->task_id,
        .expected_stimulus_round = dominoGetHumanByID(task_ptr->owner_human_id)->data->stimulus_round,
        .step_count = step_count,
        .op = op,
        .depends_current_flag = 1U,
    };
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        entry.expected_current_behavior_id_list[channel] = task_ptr->current_behavior_id_list[channel];
    }
    return entry;
}

static DominoBehaviorPlanItem planStep(uint8_t channel, uint8_t type, domino_runtime_ms_t target_ms) {
    return (DominoBehaviorPlanItem){.target_effective_ms = target_ms, .type = type, .channel_index = channel};
}

/** @brief 候选内依赖：指向本条目第 local 个步骤。 */
static DominoBehaviorDependency localDependency(uint32_t local, uint8_t kind) {
    return (DominoBehaviorDependency){.step_seq = DOMINO_PLAN_CANDIDATE_LOCAL_REF | local, .kind = kind};
}

/* ================================================================================================================ */
/* 串行场景                                                                                                           */
/* ================================================================================================================ */

/** @brief 自主睡眠闭环：加载置动机待评估标志 → 动机 → 动机集合变化发起任务规划（接受时取得 task_id 进入待执行队列，未启动）→ 行为计划 →
 * 启动时成为运行任务并建立主行为记录 → 成功计次 → 任务结束发起动机和任务规划，精力充足时任务规划给出下次规划时刻。
 */
static void testAutonomousSleep(void) {
    const uint8_t energy_list[] = {20U};
    fixtureBegin("autonomous_sleep", 1U, energy_list);
    setNowMs(1000U);
    dominoHumanExecutionAdvanceForTest(1U);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id != 0U);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE);
    DominoMotivation* motivation_ptr = restoreMotivation(1U);
    CHECK(motivation_ptr != nullptr && motivation_ptr->status == DOMINO_MOTIVATION_STATUS_ACTIVE && motivation_ptr->success_count == 0U);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_TASK].scope_kind == DOMINO_HUMAN_PLAN_SCOPE_HUMAN);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE);
    DominoTask* task_ptr = activeOrHead(1U);
    CHECK(task_ptr != nullptr && task_ptr->task_id != 0U && task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_NONE && task_ptr->plan_count == 0U);
    CHECK(human(1U)->data->active_task_id == 0U && human(1U)->data->task_queue_count == 1U);
    const domino_task_id_t task_id = task_ptr->task_id;
    motivation_ptr = restoreMotivation(1U);
    CHECK(motivation_ptr->round_task_id == task_id && motivation_ptr->need_round == 1U);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR].request_id != 0U);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE);
    task_ptr = dominoGetTaskByID(task_id);
    CHECK(task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && task_ptr->started_at_ms == 1000U && task_ptr->plan_count == 0U);
    CHECK(human(1U)->data->active_task_id == task_id && human(1U)->data->task_queue_count == 0U && human(1U)->state == DOMINO_HUMAN_STATE_NORMAL);
    const DominoBehavior* behavior_ptr = channelBehavior(1U, DOMINO_BEHAVIOR_CHANNEL_MAIN);
    CHECK(behavior_ptr != nullptr && behavior_ptr->type == DOMINO_BEHAVIOR_TYPE_SLEEP && behavior_ptr->status == DOMINO_BEHAVIOR_STATUS_ACTIVE &&
          behavior_ptr->started_at_ms == 1000U && behavior_ptr->target_effective_ms > 0U && behavior_ptr->step_seq == 1U &&
          behavior_ptr->channel_index == DOMINO_BEHAVIOR_CHANNEL_MAIN);
    CHECK(channelBehavior(1U, DOMINO_BEHAVIOR_CHANNEL_AUX_1) == nullptr && channelBehavior(1U, DOMINO_BEHAVIOR_CHANNEL_AUX_2) == nullptr);
    CHECK(runtimeOf(1U)->wakeup_game_timer_id != 0U);
    for (uint32_t step = 0U; step < 64U && dominoGetTaskByID(task_id) != nullptr; step++) {
        if (!advanceToWakeup(1U)) {
            break;
        }
    }
    motivation_ptr = restoreMotivation(1U);
    CHECK(dominoGetTaskByID(task_id) == nullptr);
    CHECK(motivation_ptr->success_count == 1U && motivation_ptr->history_count == 1U);
    const DominoMotivationHistory entry = dominoHumanMotivationHistoryAt(motivation_ptr, 0U);
    CHECK(entry.task_id == task_id && entry.started_at_ms == 1000U && entry.ended_at_ms > entry.started_at_ms);
    CHECK(human(1U)->data->energy >= g_domino_human_ai_config.sleep_target_energy);
    CHECK(motivation_ptr->round_task_id == 0U);
    CHECK(human(1U)->data->task_count == 0U && human(1U)->data->active_task_id == 0U &&
          runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    CHECK(human(1U)->state == DOMINO_HUMAN_STATE_THINKING);
    /* 精力充足时任务规划不建任务，给出精力降到触发值的下次规划时刻；唤醒只登记这一时刻，不重复制造工作。 */
    const DominoHumanRuntime* runtime_ptr = runtimeOf(1U);
    CHECK((runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) != 0U &&
          runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] > nowMs());
    CHECK(runtime_ptr->wakeup_game_timer_id != 0U && runtime_ptr->wakeup_due_ms == runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK]);
    CHECK((runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_MOTIVATION) == 0U && runtime_ptr->pending_mask == 0U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 分段结算与一次迟到结算一致；任务在较早边界结束后效果停止，人物游标仍到目标时刻；预测与真实结算共用算术且不改游标。 */
static void testSettlementEquivalence(void) {
    const uint8_t energy_list[] = {40U, 40U, 75U};
    fixtureBegin("settlement_equivalence", 3U, energy_list);
    for (domino_human_id_t human_id = 1U; human_id <= 3U; human_id++) {
        DominoMotivation* motivation_ptr = addMotivation(human_id, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
        const domino_task_id_t task_id = addRunningTask(human_id, motivation_ptr->motivation_id, DOMINO_TASK_TYPE_SLEEP, 80U);
        addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_SLEEP, human_id == 3U ? 50000U : 600000U, 0U, 0U);
        motivation_ptr = restoreMotivation(human_id);
        motivation_ptr->round_task_id = task_id;
        motivation_ptr->need_round = 1U;
    }
    DominoHumanNeedValues predicted;
    uint32_t work_count = 0U;
    CHECK(dominoHumanPredictNeedsAt(human(2U), 65000U, &predicted, &work_count));
    CHECK(human(2U)->data->settled_at_ms == 0U && human(2U)->data->energy == 40U);
    dominoHumanSettle(human(1U), 35000U);
    dominoHumanSettle(human(1U), 65000U);
    dominoHumanSettle(human(2U), 65000U);
    const DominoHumanData* a_ptr = human(1U)->data;
    const DominoHumanData* b_ptr = human(2U)->data;
    const DominoBehavior* a_behavior = channelBehavior(1U, DOMINO_BEHAVIOR_CHANNEL_MAIN);
    const DominoBehavior* b_behavior = channelBehavior(2U, DOMINO_BEHAVIOR_CHANNEL_MAIN);
    /* 65s 有效睡眠恢复 10 点，60s 自然衰减只生效一次。 */
    CHECK(a_ptr->energy == 49U && b_ptr->energy == 49U);
    CHECK(a_ptr->settled_at_ms == 65000U && b_ptr->settled_at_ms == 65000U);
    CHECK(a_behavior->effective_progress_ms == 65000U && b_behavior->effective_progress_ms == 65000U);
    CHECK(predicted.energy == b_ptr->energy);
    /* 人物 3：睡眠在第 48 秒达到 83 点、第 50 秒主行为成功并结束任务；其后效果停止，自然衰减继续到第 65 秒。 */
    const domino_task_id_t task3_id = activeOrHead(3U)->task_id;
    dominoHumanSettle(human(3U), 65000U);
    const DominoTask* task3_ptr = dominoGetTaskByID(task3_id);
    CHECK(task3_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_SUCCEEDED && task3_ptr->ended_at_ms == 50000U);
    CHECK(human(3U)->data->settled_at_ms == 65000U && human(3U)->data->energy == 82U && task3_ptr->main_succeeded_count == 1U);
    CHECK(human(3U)->data->active_task_id == 0U);
    CHECK(restoreMotivation(3U)->success_count == 1U && dominoHumanMotivationHistoryAt(restoreMotivation(3U), 0U).ended_at_ms == 50000U);
    /* 同刻再次结算不重复计算效果。 */
    dominoHumanSettle(human(3U), 65000U);
    CHECK(human(3U)->data->energy == 82U && restoreMotivation(3U)->success_count == 1U);
    fixtureEnd();
}

/** @brief 周期 1000ms、已有有效进度 900ms 的辅助通道计时行为推进 200ms：预测与真实结算都跨过一个周期边界、得到一次效果，有效进度 900 → 1100ms。 */
static void testPredictArithmetic(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("predict_arithmetic", 1U, energy_list);
    DominoMotivation* motivation_ptr = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT);
    const domino_task_id_t task_id = addRunningTask(1U, motivation_ptr->motivation_id, DOMINO_TASK_TYPE_PHONE_CALL, 0U);
    const domino_behavior_id_t behavior_id =
        addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_AUX_1, DOMINO_BEHAVIOR_TYPE_WORK, 100000U, 1000U, 0U);
    dominoGetBehaviorByID(behavior_id)->effective_progress_ms = 900U;
    DominoHumanNeedValues predicted;
    uint32_t work_count = 0U;
    CHECK(dominoHumanPredictNeedsAt(human(1U), 200U, &predicted, &work_count));
    CHECK(predicted.energy == 49U && human(1U)->data->energy == 50U && human(1U)->data->settled_at_ms == 0U &&
          dominoGetBehaviorByID(behavior_id)->effective_progress_ms == 900U);
    dominoHumanSettle(human(1U), 200U);
    CHECK(human(1U)->data->energy == 49U && dominoGetBehaviorByID(behavior_id)->effective_progress_ms == 1100U);
    CHECK(human(1U)->data->settled_at_ms == 200U);
    /* 预测下一检查边界严格晚于已结算基准。 */
    domino_runtime_ms_t due_ms;
    CHECK(dominoHumanPredictNextCheck(human(1U), &due_ms) && due_ms > 200U);
    fixtureEnd();
}

/** @brief 自然需求对齐业务时间整分钟边界：游标不在整分钟上时首次衰减发生在下一个整分钟；分段、一次结算、预测、阈值时刻和睡眠估时同口径。 */
static void testNaturalMinuteAlignment(void) {
    const uint8_t energy_list[] = {50U, 50U};
    fixtureBegin("natural_minute_alignment", 2U, energy_list);
    DominoHumanData* a_ptr = human(1U)->data;
    DominoHumanData* b_ptr = human(2U)->data;
    a_ptr->settled_at_ms = 30000U;
    b_ptr->settled_at_ms = 30000U;
    /* 人物 1 分段：第 30s → 59.999s 不跨整分钟，第 60s 衰减一次，再到 179.999s 只跨第 120s。 */
    dominoHumanSettle(human(1U), 59999U);
    CHECK(a_ptr->energy == 50U);
    dominoHumanSettle(human(1U), 60000U);
    CHECK(a_ptr->energy == 49U && a_ptr->settled_at_ms == 60000U);
    dominoHumanSettle(human(1U), 179999U);
    CHECK(a_ptr->energy == 48U);
    /* 人物 2 先只读预测再一次结算，与人物 1 分段结果一致。 */
    DominoHumanNeedValues predicted;
    uint32_t work_count = 0U;
    CHECK(dominoHumanPredictNeedsAt(human(2U), 179999U, &predicted, &work_count) && predicted.energy == 48U && b_ptr->energy == 50U);
    dominoHumanSettle(human(2U), 179999U);
    CHECK(b_ptr->energy == 48U && b_ptr->settled_at_ms == 179999U);
    /* 精力 50 降到 48 需要两次整分钟衰减：从第 30s 起在第 120s 达到，从第 0s 起同样在第 120s 达到。 */
    CHECK(dominoHumanNaturalMsUntilEnergyAtMost(50U, 30000U, 48U) == 90000U);
    CHECK(dominoHumanNaturalMsUntilEnergyAtMost(50U, 0U, 48U) == 120000U);
    /* 睡眠估时：精力 40 睡到 50。从第 0s 起第 60s 的衰减与第 10 点恢复同刻，66s 达到；从第 59s 起要多承担第 60s 和第 120s 两次衰减，72s 达到。 */
    domino_runtime_ms_t duration_ms = 0U;
    work_count = 0U;
    CHECK(dominoHumanCalcSleepDuration(40U, 0U, 50U, 1000U, &duration_ms, &work_count) && duration_ms == 66000U);
    CHECK(dominoHumanCalcSleepDuration(40U, 59000U, 50U, 1000U, &duration_ms, &work_count) && duration_ms == 72000U);
    fixtureEnd();
}

/** @brief 推进到睡眠任务刚接受（未启动、在待执行队列）或刚启动（运行任务）的状态。 */
static domino_task_id_t prepareSleepTask(domino_human_id_t human_id, bool start_flag) {
    setNowMs(1000U);
    dominoHumanExecutionAdvanceForTest(human_id);
    serveRequest(human_id, DOMINO_HUMAN_PLANNER_MOTIVATION, nullptr);
    serveRequest(human_id, DOMINO_HUMAN_PLANNER_TASK, nullptr);
    if (start_flag) {
        serveRequest(human_id, DOMINO_HUMAN_PLANNER_BEHAVIOR, nullptr);
    }
    const DominoTask* task_ptr = activeOrHead(human_id);
    return task_ptr ? task_ptr->task_id : 0U;
}

/** @brief 外部补足精力：未启动计划撤销并回收（task_id 不复用），已运行任务取消；均不计次，以后新需求使用新轮次。 */
static void testExternalSatisfaction(void) {
    const uint8_t energy_list[] = {20U, 20U};
    fixtureBegin("external_satisfaction", 2U, energy_list);
    const domino_task_id_t planned_id = prepareSleepTask(1U, false);
    CHECK(planned_id != 0U && dominoGetTaskByID(planned_id)->exec_status == DOMINO_TASK_EXEC_STATUS_NONE);
    setNowMs(2000U);
    submitEnergyForTest(1U, 90U);
    CHECK(dominoGetTaskByID(planned_id) == nullptr && human(1U)->data->task_count == 0U && human(1U)->data->task_queue_count == 0U);
    CHECK(restoreMotivation(1U)->success_count == 0U && restoreMotivation(1U)->round_task_id == 0U && restoreMotivation(1U)->need_round == 1U);
    CHECK(restoreMotivation(1U)->last_failure_reason == DOMINO_END_REASON_NEED_SATISFIED);
    /* 新需求出现时按新轮次重新规划，新 task_id 不复用旧值。 */
    setNowMs(3000U);
    submitEnergyForTest(1U, 25U);
    pumpRequests(1U);
    const DominoTask* round2_ptr = activeOrHead(1U);
    CHECK(round2_ptr != nullptr && round2_ptr->task_id > planned_id && round2_ptr->work_round == 2U && restoreMotivation(1U)->need_round == 2U);

    const domino_task_id_t running_id = prepareSleepTask(2U, true);
    CHECK(running_id != 0U && dominoGetTaskByID(running_id)->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN);
    const domino_behavior_id_t behavior_id = dominoGetTaskByID(running_id)->current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_MAIN];
    setNowMs(7000U);
    submitEnergyForTest(2U, 90U);
    CHECK(dominoGetTaskByID(running_id) == nullptr && dominoGetBehaviorByID(behavior_id) == nullptr);
    CHECK(restoreMotivation(2U)->success_count == 0U && restoreMotivation(2U)->last_failure_reason == DOMINO_END_REASON_NEED_SATISFIED);
    CHECK(human(2U)->data->active_task_id == 0U && human(2U)->data->energy == 90U);
    fixtureEnd();
}

/** @brief 睡眠条件失效：同条消息内按旧条件结算、取消主行为并归并进度，任务保持 OPEN 并仍是运行任务；恢复后行为规划器修复，替代行为取得新 ID。 */
static void testSleepConditionRepair(void) {
    const uint8_t energy_list[] = {20U};
    fixtureBegin("sleep_condition_repair", 1U, energy_list);
    const domino_task_id_t task_id = prepareSleepTask(1U, true);
    const domino_behavior_id_t first_behavior_id = dominoGetTaskByID(task_id)->current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_MAIN];
    setNowMs(11000U);
    submitConditionForTest(1U, true);
    const DominoTask* task_ptr = dominoGetTaskByID(task_id);
    CHECK(task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && task_ptr->current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_MAIN] == 0U &&
          task_ptr->main_cancelled_count == 1U);
    CHECK(task_ptr->progress_ms == 10000U && task_ptr->main_succeeded_count == 0U && task_ptr->last_behavior_id == first_behavior_id);
    CHECK(dominoGetBehaviorByID(first_behavior_id)->status == DOMINO_BEHAVIOR_STATUS_CANCELLED &&
          dominoGetBehaviorByID(first_behavior_id)->ended_at_ms == 11000U);
    CHECK(human(1U)->data->active_task_id == task_id);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR].request_id == 0U && human(1U)->state == DOMINO_HUMAN_STATE_THINKING);
    /* 相同条件值不重复处置。 */
    submitConditionForTest(1U, true);
    CHECK(dominoGetTaskByID(task_id)->main_cancelled_count == 1U);
    setNowMs(12000U);
    submitConditionForTest(1U, false);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR].request_id != 0U);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR].priority == DOMINO_HUMAN_PLAN_PRIORITY_HIGH);
    serveRequest(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, nullptr);
    task_ptr = dominoGetTaskByID(task_id);
    const domino_behavior_id_t second_behavior_id = task_ptr->current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_MAIN];
    CHECK(second_behavior_id != 0U && second_behavior_id > first_behavior_id && task_ptr->started_at_ms == 1000U);
    CHECK(task_ptr->progress_ms == 10000U && dominoGetBehaviorByID(second_behavior_id)->started_at_ms == 12000U &&
          dominoGetBehaviorByID(second_behavior_id)->step_seq == 2U);
    fixtureEnd();
}

/** @brief 实施历史：失败不计次；第 11 次成功后保留最近 10 条、累计 11。 */
static void testMotivationHistory(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("motivation_history", 1U, energy_list);
    DominoMotivation* motivation_ptr = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
    DominoTask failed = {.task_id = 50U, .exec_status = DOMINO_TASK_EXEC_STATUS_FAILED, .end_reason = DOMINO_END_REASON_DEADLINE_MISSED};
    dominoHumanMotivationRecordTaskEnded(motivation_ptr, &failed);
    for (uint32_t index = 1U; index <= 11U; index++) {
        DominoTask task = {
            .task_id = 100U + index, .exec_status = DOMINO_TASK_EXEC_STATUS_SUCCEEDED, .started_at_ms = index * 10U, .ended_at_ms = index * 10U + 5U};
        dominoHumanMotivationRecordTaskEnded(motivation_ptr, &task);
    }
    CHECK(motivation_ptr->success_count == 11U && motivation_ptr->history_count == 10U);
    CHECK(dominoHumanMotivationHistoryAt(motivation_ptr, 0U).task_id == 102U && dominoHumanMotivationHistoryAt(motivation_ptr, 9U).task_id == 111U);
    CHECK(dominoHumanMotivationHistoryAt(motivation_ptr, 9U).started_at_ms == 110U &&
          dominoHumanMotivationHistoryAt(motivation_ptr, 9U).ended_at_ms == 115U);
    CHECK(motivation_ptr->last_failure_reason == DOMINO_END_REASON_DEADLINE_MISSED);
    fixtureEnd();
}

/** @brief 请求身份与请求不设期限：旧或另一优先级的结果不清新资格；投递请求不改变人物唤醒；排队超过原 3000ms 期限的请求照常计算，
 * 结果照常处理，不出现重试和重新投递。
 */
static void testRequestIdentity(void) {
    const uint8_t energy_list[] = {20U};
    fixtureBegin("request_identity", 1U, energy_list);
    setNowMs(1000U);
    dominoHumanExecutionAdvanceForTest(1U);
    const DominoHumanRuntime* runtime_ptr = runtimeOf(1U);
    const DominoHumanPlanSlot slot = runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION];
    CHECK(slot.request_id != 0U && slot.priority == DOMINO_HUMAN_PLAN_PRIORITY_NORMAL && slot.scope_kind == DOMINO_HUMAN_PLAN_SCOPE_HUMAN);
    /* 待评估标志在投递时清除；投递请求不改变人物唤醒：没有任务、行为重试和下次规划时刻，人物不登记唤醒。 */
    CHECK(runtime_ptr->pending_mask == 0U && runtime_ptr->wakeup_game_timer_id == 0U);
    DominoHumanPlanResultMessage stale = {.request_id = slot.request_id + 7U, .human_id = 1U, .outcome = DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE};
    dominoHumanExecutionProcessResultForTest(DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_PRIORITY_NORMAL, &stale);
    DominoHumanPlanResultMessage wrong_priority = {.request_id = slot.request_id, .human_id = 1U, .outcome = DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE};
    dominoHumanExecutionProcessResultForTest(DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_PRIORITY_HIGH, &wrong_priority);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id == slot.request_id);
    CHECK(dominoHumanExecutionGetStats().stale_result_count == 2U && runtime_ptr->next_plan_mask == 0U);
    /* 排队 9 秒：推进人物不撤销请求，也不重新投递。 */
    const uint64_t posted_count = dominoHumanExecutionGetStats().posted_request_count_list[DOMINO_HUMAN_PLANNER_MOTIVATION];
    advanceAt(1U, 10000U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id == slot.request_id);
    /* 结果照常计算并提交，之后没有重试，也没有新的动机请求。 */
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE && restoreMotivation(1U) != nullptr);
    CHECK(dominoHumanExecutionGetStats().posted_request_count_list[DOMINO_HUMAN_PLANNER_MOTIVATION] == posted_count);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id == 0U &&
          (runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_MOTIVATION) == 0U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 受控任务候选：一份候选可为多个动机建立任务；必要部分不成立整份拒绝并设兜底时刻；必要部分成立时纳入成立的可选项、
 * 省略不成立的可选项（包括过期队列版本的插队），新任务按顺序进入同一条待执行队列；同一工作再次建议复用原任务；
 * 每人 32 个任务的容量上限按必要／可选规则拒绝。
 */
static void testCandidateCommit(void) {
    const uint8_t energy_list[] = {20U};
    fixtureBegin("candidate_commit", 1U, energy_list);
    setNowMs(1000U);
    const DominoHumanRuntime* runtime_ptr = runtimeOf(1U);
    const domino_motivation_id_t restore_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY)->motivation_id;
    const domino_motivation_id_t contact_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
    const DominoTaskCandidateEntry sleep_entry = sleepEntry(restore_id, 0U, 1U);
    DominoTaskCandidateEntry phone_entry = phoneEntry(contact_id, 1U);
    DominoTaskCandidateEntry bad_entry = phone_entry;
    bad_entry.type = DOMINO_TASK_TYPE_NONE;
    DominoTaskCandidateEntry stale_insert_entry = phone_entry;
    stale_insert_entry.work_round = 9U;
    stale_insert_entry.insert_index = 0U;
    stale_insert_entry.expected_queue_version = 777U;
    /* 必要部分不成立：整份拒绝，下次规划时刻设为处理时刻 + retry_delay_ms。 */
    const DominoTaskCandidateEntry invalid_list[] = {bad_entry, sleep_entry};
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(2U, 1U, invalid_list)));
    CHECK(human(1U)->data->task_count == 0U && dominoHumanExecutionGetStats().rejected_candidate_count == 1U);
    CHECK((runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) != 0U && runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] == 4000U);
    /* 必要部分成立：一份候选为两个动机各建一项任务，省略非法类型和过期队列版本的插队；结果给出“无”，清除兜底时刻。 */
    const DominoTaskCandidateEntry valid_list[] = {sleep_entry, phone_entry, bad_entry, stale_insert_entry};
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(4U, 1U, valid_list)));
    const DominoHumanData* data_ptr = human(1U)->data;
    CHECK(data_ptr->task_count == 2U && data_ptr->task_queue_count == 2U && data_ptr->active_task_id == 0U);
    CHECK((runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U);
    const DominoTask* sleep_ptr = dominoGetTaskByID(data_ptr->task_queue_list[0]);
    const DominoTask* phone_ptr = dominoGetTaskByID(data_ptr->task_queue_list[1]);
    CHECK(sleep_ptr != nullptr && sleep_ptr->type == DOMINO_TASK_TYPE_SLEEP && sleep_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_NONE);
    CHECK(phone_ptr != nullptr && phone_ptr->type == DOMINO_TASK_TYPE_PHONE_CALL && phone_ptr->motivation_id == contact_id);
    CHECK((phone_ptr->flags & DOMINO_TASK_FLAG_PREEMPT) == 0U && phone_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE);
    CHECK(restoreMotivation(1U)->round_task_id == sleep_ptr->task_id && restoreMotivation(1U)->need_round == 1U);
    /* 同一工作再次建议：已有未终结任务的工作身份相同，复用原 task_id，不新建。 */
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &sleep_entry)));
    CHECK(human(1U)->data->task_count == 2U);
    /* 容量：每人最多 32 个任务，补满后再建议的必要条目被拒绝。 */
    DominoTaskCandidateEntry fill_list[DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX];
    for (uint32_t batch = 0U; batch < 4U; batch++) {
        const uint8_t count = batch < 3U ? DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX : 6U;
        for (uint8_t index = 0U; index < count; index++) {
            fill_list[index] = phoneEntry(contact_id, 100U + (batch * DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX) + index);
        }
        CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(count, count, fill_list)));
    }
    CHECK(human(1U)->data->task_count == DOMINO_HUMAN_TASK_COUNT_MAX);
    phone_entry.work_round = 200U;
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &phone_entry)));
    CHECK(human(1U)->data->task_count == DOMINO_HUMAN_TASK_COUNT_MAX);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 信息差：任务规划器在精力低时提出下一轮睡眠，提交前精力被外部补足，结果仍照常提交（只复核轮次锚点），随后由任务决策以需求已满足撤销；
 * 标志保留的任务规划随即发起并给出下次规划时刻。按旧轮次读取的建议、上一轮尚未结束时的下一轮建议被拒绝，同一工作再次建议复用原任务。
 */
static void testInformationGap(void) {
    const uint8_t energy_list[] = {25U};
    fixtureBegin("information_gap", 1U, energy_list);
    setNowMs(1000U);
    dominoHumanSettle(human(1U), 1000U);
    DominoMotivation* motivation_ptr = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
    motivation_ptr->need_round = 1U;
    const domino_motivation_id_t restore_id = motivation_ptr->motivation_id;
    /* 规划器在精力 25 时读取快照，提出第 2 轮睡眠且不要求以后再看。 */
    issueSlot(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U);
    DominoHumanPlanOutput snapshot = {0};
    CHECK(dominoHumanPlanTasks(human(1U), &runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_TASK], nowMs(), 512U, &snapshot) ==
              DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE &&
          !snapshot.next_plan_valid_flag);
    /* 提交前精力被外部补到 90：执行器不复核精力，结果照常提交；未启动的任务随后被任务决策撤销，不计次。 */
    submitEnergyForTest(1U, 90U);
    CHECK((runtimeOf(1U)->pending_mask & DOMINO_HUMAN_PLAN_MASK_TASK) != 0U);
    CHECK(serveControlled(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE, snapshot.candidate_ptr, false, 0U));
    motivation_ptr = restoreMotivation(1U);
    CHECK(motivation_ptr->need_round == 2U && motivation_ptr->round_task_id == 0U && motivation_ptr->success_count == 0U);
    CHECK(motivation_ptr->last_failure_reason == DOMINO_END_REASON_NEED_SATISFIED && human(1U)->data->task_count == 0U);
    /* 请求在途期间置位的任务待规划标志保留：结果处理后再发起一次，精力充足时 NO_CHANGE 并给出下次规划时刻。 */
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE);
    CHECK((runtimeOf(1U)->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) != 0U);
    /* 按旧轮次读取的建议：读取时轮次与当前轮次不符，拒绝。 */
    submitEnergyForTest(1U, 25U);
    const DominoTaskCandidateEntry stale_entry = sleepEntry(restore_id, 1U, 2U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &stale_entry)));
    /* 第 3 轮被接受后，上一轮尚未结束时的第 4 轮建议被拒绝；第 3 轮再次建议复用原任务。 */
    const DominoTaskCandidateEntry round3_entry = sleepEntry(restore_id, 2U, 3U);
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &round3_entry)));
    const domino_task_id_t round3_id = restoreMotivation(1U)->round_task_id;
    CHECK(round3_id != 0U && human(1U)->data->task_count == 1U);
    const DominoTaskCandidateEntry round4_entry = sleepEntry(restore_id, 3U, 4U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &round4_entry)));
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &round3_entry)));
    CHECK(human(1U)->data->task_count == 1U && restoreMotivation(1U)->round_task_id == round3_id && restoreMotivation(1U)->need_round == 3U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 同一任务内的行为依赖：
 * - 人物 1：A（主）完成依赖 B（辅 1），B 伴随 C（辅 2）。A 自身先达标但等 B；B 在第 3 秒成功后 A 同一边界成功；C 第 5 秒成功后任务成功。
 * - 人物 2：B 伴随 C 且远未完成，A 完成依赖 B。C 第 5 秒成功时 B 随之取消，A 的完成依赖失效而失败，三者同一边界结束；任务保持 OPEN 等待修复。
 */
static void testBehaviorDependencies(void) {
    const uint8_t energy_list[] = {50U, 50U};
    fixtureBegin("behavior_dependencies", 2U, energy_list);
    domino_task_id_t task_id_list[2];
    domino_behavior_id_t a_id_list[2];
    for (domino_human_id_t human_id = 1U; human_id <= 2U; human_id++) {
        const domino_motivation_id_t motivation_id = addMotivation(human_id, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
        const domino_task_id_t task_id = addRunningTask(human_id, motivation_id, DOMINO_TASK_TYPE_MESSAGE, 0U);
        const domino_behavior_id_t c_id = addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_AUX_2, DOMINO_BEHAVIOR_TYPE_MESSAGE, 5000U, 0U, 0U);
        const domino_behavior_id_t b_id =
            addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_AUX_1, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, human_id == 1U ? 3000U : 100000U, 0U, 0U);
        const domino_behavior_id_t a_id = addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_WORK, 1000U, 0U, 0U);
        addBehaviorDependency(b_id, c_id, DOMINO_BEHAVIOR_DEPENDENCY_WHILE);
        addBehaviorDependency(a_id, b_id, DOMINO_BEHAVIOR_DEPENDENCY_COMPLETE);
        task_id_list[human_id - 1U] = task_id;
        a_id_list[human_id - 1U] = a_id;
    }
    /* 人物 1 第 1 秒：A 自身已达标但完成依赖未到，继续保持 ACTIVE。 */
    dominoHumanSettle(human(1U), 2000U);
    CHECK(dominoGetBehaviorByID(a_id_list[0])->status == DOMINO_BEHAVIOR_STATUS_ACTIVE &&
          dominoGetBehaviorByID(a_id_list[0])->effective_progress_ms == 1000U);
    domino_runtime_ms_t due_ms;
    CHECK(dominoHumanPredictNextCheck(human(1U), &due_ms) && due_ms == 3000U);
    dominoHumanSettle(human(1U), 3000U);
    const DominoTask* task1_ptr = dominoGetTaskByID(task_id_list[0]);
    const DominoBehavior* a1_ptr = dominoGetBehaviorByID(a_id_list[0]);
    CHECK(a1_ptr != nullptr && a1_ptr->status == DOMINO_BEHAVIOR_STATUS_SUCCEEDED && a1_ptr->ended_at_ms == 3000U);
    CHECK(task1_ptr->current_behavior_id_list[0] == 0U && task1_ptr->current_behavior_id_list[1] == 0U &&
          task1_ptr->current_behavior_id_list[2] != 0U);
    CHECK(task1_ptr->main_succeeded_count == 1U && task1_ptr->progress_ms == 1000U && task1_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN);
    dominoHumanSettle(human(1U), 7000U);
    task1_ptr = dominoGetTaskByID(task_id_list[0]);
    CHECK(task1_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_SUCCEEDED && task1_ptr->ended_at_ms == 5000U);
    CHECK(contactMotivation(1U)->success_count == 1U && human(1U)->data->active_task_id == 0U);

    dominoHumanSettle(human(2U), 7000U);
    const DominoTask* task2_ptr = dominoGetTaskByID(task_id_list[1]);
    const DominoBehavior* a2_ptr = dominoGetBehaviorByID(a_id_list[1]);
    CHECK(a2_ptr != nullptr && a2_ptr->status == DOMINO_BEHAVIOR_STATUS_FAILED && a2_ptr->end_reason == DOMINO_END_REASON_DEPENDENCY_BROKEN &&
          a2_ptr->ended_at_ms == 5000U);
    CHECK(task2_ptr->current_behavior_id_list[0] == 0U && task2_ptr->current_behavior_id_list[1] == 0U &&
          task2_ptr->current_behavior_id_list[2] == 0U);
    CHECK(task2_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && task2_ptr->main_failed_count == 1U && task2_ptr->main_succeeded_count == 0U);
    CHECK(human(2U)->data->settled_at_ms == 7000U && dominoHumanTaskNeedsBehaviorPlan(human(2U), task2_ptr));
    fixtureEnd();
}

/** @brief 行为候选的依赖审核与启动：
 * - 启动依赖等来源成功后才启动；伴随依赖可指向同批刚启动的行为，来源结束时随之取消；任务等自身全部工作完成后成功。
 * - 依赖指向本条目更晚的步骤、伴随依赖指向同一通道、依赖不存在的当前步骤或 REPLACE 自带插入标志时，整份拒绝。
 */
static void testBehaviorCandidateDependencies(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("behavior_candidate_dependencies", 1U, energy_list);
    setNowMs(1000U);
    const domino_motivation_id_t motivation_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
    dominoHumanSettle(human(1U), 1000U);
    const domino_task_id_t task_id = addRunningTask(1U, motivation_id, DOMINO_TASK_TYPE_MESSAGE, 0U);
    const DominoTask* task_ptr = dominoGetTaskByID(task_id);
    /* 非法：依赖更晚的步骤。 */
    DominoBehaviorPlanItem bad_list[2] = {planStep(0U, DOMINO_BEHAVIOR_TYPE_WORK, 2000U), planStep(1U, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 1000U)};
    bad_list[0].dependency_list[0] = localDependency(1U, DOMINO_BEHAVIOR_DEPENDENCY_START);
    DominoBehaviorCandidateEntry entry = behaviorEntry(task_ptr, DOMINO_BEHAVIOR_CANDIDATE_REPLACE, 2U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &entry, 2U, bad_list)));
    /* 非法：伴随依赖指向同一通道。 */
    bad_list[0] = planStep(0U, DOMINO_BEHAVIOR_TYPE_WORK, 2000U);
    bad_list[1] = planStep(0U, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 1000U);
    bad_list[1].dependency_list[0] = localDependency(0U, DOMINO_BEHAVIOR_DEPENDENCY_WHILE);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &entry, 2U, bad_list)));
    /* 非法：依赖不存在的当前步骤；REPLACE 自带插入标志。 */
    bad_list[1] = planStep(1U, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 1000U);
    bad_list[1].dependency_list[0] = (DominoBehaviorDependency){.step_seq = 42U, .kind = DOMINO_BEHAVIOR_DEPENDENCY_START};
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &entry, 2U, bad_list)));
    bad_list[1] = planStep(1U, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 1000U);
    bad_list[1].flags = DOMINO_BEHAVIOR_FLAG_INSERTED;
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &entry, 2U, bad_list)));
    task_ptr = dominoGetTaskByID(task_id);
    CHECK(task_ptr->plan_count == 0U && task_ptr->last_step_seq == 0U && dominoHumanExecutionGetStats().rejected_candidate_count == 4U);
    /* 合法：s0 主通道工作 2s；s1 辅 1 通话 1s，启动依赖 s0；s2 辅 2 消息 5s，伴随 s0。 */
    DominoBehaviorPlanItem step_list[3] = {planStep(0U, DOMINO_BEHAVIOR_TYPE_WORK, 2000U), planStep(1U, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 1000U),
                                           planStep(2U, DOMINO_BEHAVIOR_TYPE_MESSAGE, 5000U)};
    step_list[1].dependency_list[0] = localDependency(0U, DOMINO_BEHAVIOR_DEPENDENCY_START);
    step_list[2].dependency_list[0] = localDependency(0U, DOMINO_BEHAVIOR_DEPENDENCY_WHILE);
    entry = behaviorEntry(task_ptr, DOMINO_BEHAVIOR_CANDIDATE_REPLACE, 3U);
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                          buildBehaviorCandidate(1U, 1U, &entry, 3U, step_list)));
    task_ptr = dominoGetTaskByID(task_id);
    const DominoBehavior* s0_ptr = channelBehavior(1U, 0U);
    const DominoBehavior* s2_ptr = channelBehavior(1U, 2U);
    CHECK(s0_ptr != nullptr && s0_ptr->step_seq == 1U && s0_ptr->started_at_ms == 1000U);
    CHECK(channelBehavior(1U, 1U) == nullptr && task_ptr->plan_count == 1U && task_ptr->plan_item_list[0].step_seq == 2U);
    CHECK(task_ptr->plan_item_list[0].dependency_list[0].step_seq == 1U &&
          task_ptr->plan_item_list[0].dependency_list[0].state == DOMINO_BEHAVIOR_DEPENDENCY_PENDING);
    CHECK(s2_ptr != nullptr && s2_ptr->step_seq == 3U && s2_ptr->dependency_list[0].kind == DOMINO_BEHAVIOR_DEPENDENCY_WHILE &&
          s2_ptr->dependency_list[0].step_seq == 1U && task_ptr->last_step_seq == 3U);
    const domino_behavior_id_t s2_id = s2_ptr->behavior_id;
    /* 第 3 秒：s0 成功，s2 随之取消，s1 的启动依赖满足并在同一时刻启动。 */
    advanceAt(1U, 3000U);
    task_ptr = dominoGetTaskByID(task_id);
    const DominoBehavior* s1_ptr = channelBehavior(1U, 1U);
    CHECK(channelBehavior(1U, 0U) == nullptr && channelBehavior(1U, 2U) == nullptr && task_ptr->plan_count == 0U);
    CHECK(s1_ptr != nullptr && s1_ptr->step_seq == 2U && s1_ptr->started_at_ms == 3000U);
    CHECK(dominoGetBehaviorByID(s2_id) == nullptr || (dominoGetBehaviorByID(s2_id)->status == DOMINO_BEHAVIOR_STATUS_CANCELLED &&
                                                      dominoGetBehaviorByID(s2_id)->end_reason == DOMINO_END_REASON_DEPENDENCY_BROKEN));
    CHECK(task_ptr->main_succeeded_count == 1U && task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN);
    /* 第 4 秒：s1 成功，自身工作全部完成，任务成功。 */
    advanceAt(1U, 4000U);
    CHECK(dominoGetTaskByID(task_id) == nullptr && contactMotivation(1U)->success_count == 1U);
    CHECK(dominoHumanMotivationHistoryAt(contactMotivation(1U), 0U).ended_at_ms == 4000U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 截止第 10 秒的任务，主行为完成依赖辅助行为（收款）：收款恰在第 10 秒成功且截止点包含时任务成功；
 * 收款晚到或要求严格早于截止点时，任务在截止点失败。辅助行为所在通道不影响判定先后。
 */
static void testDeadlineBoundary(void) {
    const uint8_t energy_list[] = {50U, 50U, 50U, 50U};
    fixtureBegin("deadline_boundary", 4U, energy_list);
    const domino_runtime_ms_t payment_list[] = {10000U, 10000U, 11000U, 10000U};
    const bool inclusive_list[] = {true, true, true, false};
    const uint8_t channel_list[] = {1U, 2U, 1U, 1U};
    const uint8_t expected_list[] = {DOMINO_TASK_EXEC_STATUS_SUCCEEDED, DOMINO_TASK_EXEC_STATUS_SUCCEEDED, DOMINO_TASK_EXEC_STATUS_FAILED,
                                     DOMINO_TASK_EXEC_STATUS_FAILED};
    for (domino_human_id_t human_id = 1U; human_id <= 4U; human_id++) {
        const uint32_t index = human_id - 1U;
        const domino_motivation_id_t motivation_id = addMotivation(human_id, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
        const domino_task_id_t task_id = addRunningTask(human_id, motivation_id, DOMINO_TASK_TYPE_PHONE_CALL, 0U);
        const domino_behavior_id_t payment_id =
            addRunningBehavior(task_id, channel_list[index], DOMINO_BEHAVIOR_TYPE_MESSAGE, payment_list[index], 0U, 0U);
        const domino_behavior_id_t order_id = addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_WORK, 1000U, 0U, 0U);
        addBehaviorDependency(order_id, payment_id, DOMINO_BEHAVIOR_DEPENDENCY_COMPLETE);
        setDeadline(task_id, 10000U, inclusive_list[index]);
        dominoHumanSettle(human(human_id), 12000U);
        const DominoTask* task_ptr = dominoGetTaskByID(task_id);
        CHECK(task_ptr->exec_status == expected_list[index] && task_ptr->ended_at_ms == 10000U);
        CHECK(task_ptr->end_reason ==
              (expected_list[index] == DOMINO_TASK_EXEC_STATUS_SUCCEEDED ? DOMINO_END_REASON_COMPLETED : DOMINO_END_REASON_DEADLINE_MISSED));
    }
    fixtureEnd();
}

/** @brief 终态明细保留或回收不改变任务结果；部分进度在终态提交时归入任务，回收、重复结算不再累计。 */
static void testTerminalDetailRetention(void) {
    const uint8_t energy_list[] = {70U, 40U};
    fixtureBegin("terminal_detail_retention", 2U, energy_list);
    domino_task_id_t task_id_list[2];
    for (domino_human_id_t human_id = 1U; human_id <= 2U; human_id++) {
        DominoMotivation* motivation_ptr = addMotivation(human_id, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
        const domino_task_id_t task_id = addRunningTask(human_id, motivation_ptr->motivation_id, DOMINO_TASK_TYPE_SLEEP, 80U);
        addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_SLEEP, human_id == 1U ? 6000U : 60000U, 0U, 0U);
        restoreMotivation(human_id)->round_task_id = task_id;
        task_id_list[human_id - 1U] = task_id;
    }
    dominoHumanSettle(human(1U), 6000U);
    DominoTask* task1_ptr = dominoGetTaskByID(task_id_list[0]);
    CHECK(task1_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && task1_ptr->main_succeeded_count == 1U && task1_ptr->last_behavior_id != 0U);
    dominoHumanStoreReclaimBehavior(task1_ptr, task1_ptr->last_behavior_id);
    task1_ptr->last_behavior_id = 0U;
    setNowMs(6000U);
    submitEnergyForTest(1U, 85U);
    CHECK(dominoGetTaskByID(task_id_list[0]) == nullptr && restoreMotivation(1U)->success_count == 1U);
    CHECK(dominoHumanMotivationHistoryAt(restoreMotivation(1U), 0U).task_id == task_id_list[0]);

    setNowMs(3000U);
    submitConditionForTest(2U, true);
    DominoTask* task2_ptr = dominoGetTaskByID(task_id_list[1]);
    CHECK(task2_ptr->progress_ms == 3000U && task2_ptr->main_cancelled_count == 1U && task2_ptr->main_succeeded_count == 0U);
    dominoHumanStoreReclaimBehavior(task2_ptr, task2_ptr->last_behavior_id);
    task2_ptr->last_behavior_id = 0U;
    setNowMs(9000U);
    dominoHumanExecutionAdvanceForTest(2U);
    task2_ptr = dominoGetTaskByID(task_id_list[1]);
    CHECK(task2_ptr->progress_ms == 3000U && task2_ptr->main_cancelled_count == 1U && restoreMotivation(2U)->success_count == 0U);
    fixtureEnd();
}

/** @brief 同一边界主行为睡眠把精力从 79 提到 80、同一任务的辅助行为再消耗 1 点：按最终 79 判定，不成功，留下修复需要；下一检查严格晚于本边界。 */
static void testSameBoundaryInterplay(void) {
    const uint8_t energy_list[] = {79U};
    fixtureBegin("same_boundary_interplay", 1U, energy_list);
    const domino_motivation_id_t restore_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY)->motivation_id;
    const domino_task_id_t sleep_id = addRunningTask(1U, restore_id, DOMINO_TASK_TYPE_SLEEP, 80U);
    addRunningBehavior(sleep_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_SLEEP, 6000U, 0U, 0U);
    addRunningBehavior(sleep_id, DOMINO_BEHAVIOR_CHANNEL_AUX_1, DOMINO_BEHAVIOR_TYPE_WORK, 60000U, 6000U, 0U);
    restoreMotivation(1U)->round_task_id = sleep_id;
    dominoHumanSettle(human(1U), 6000U);
    const DominoTask* sleep_ptr = dominoGetTaskByID(sleep_id);
    CHECK(human(1U)->data->energy == 79U && sleep_ptr->main_succeeded_count == 1U);
    CHECK(sleep_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && sleep_ptr->current_behavior_id_list[0] == 0U &&
          sleep_ptr->current_behavior_id_list[1] != 0U);
    CHECK(dominoHumanTaskNeedsBehaviorPlan(human(1U), sleep_ptr));
    domino_runtime_ms_t due_ms;
    CHECK(dominoHumanPredictNextCheck(human(1U), &due_ms) && due_ms > 6000U);
    /* 不推进时间的同刻复查不重复结算。 */
    dominoHumanResolveBoundary(human(1U));
    CHECK(human(1U)->data->energy == 79U && dominoGetTaskByID(sleep_id)->main_succeeded_count == 1U);
    fixtureEnd();
}

/** @brief 通知晚到：第 6 秒已满足完成条件，第 10 秒才处理；任务在真实边界结束，其后不再产生睡眠效果。 */
static void testLateNotification(void) {
    const uint8_t energy_list[] = {79U};
    fixtureBegin("late_notification", 1U, energy_list);
    DominoMotivation* motivation_ptr = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
    const domino_task_id_t task_id = addRunningTask(1U, motivation_ptr->motivation_id, DOMINO_TASK_TYPE_SLEEP, 80U);
    addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_SLEEP, 6000U, 0U, 0U);
    restoreMotivation(1U)->round_task_id = task_id;
    setNowMs(10000U);
    dominoHumanExecutionAdvanceForTest(1U);
    CHECK(dominoGetTaskByID(task_id) == nullptr && restoreMotivation(1U)->success_count == 1U);
    CHECK(dominoHumanMotivationHistoryAt(restoreMotivation(1U), 0U).ended_at_ms == 6000U && human(1U)->data->energy == 80U);
    CHECK(human(1U)->data->settled_at_ms == 10000U);
    fixtureEnd();
}

/** @brief 不同触发路径共用任务决策：定时推进与条件处理在相同事实下得到相同的修复结果。 */
static void testSharedDecision(void) {
    const uint8_t energy_list[] = {40U, 40U};
    fixtureBegin("shared_decision", 2U, energy_list);
    for (domino_human_id_t human_id = 1U; human_id <= 2U; human_id++) {
        DominoMotivation* motivation_ptr = addMotivation(human_id, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
        const domino_task_id_t task_id = addRunningTask(human_id, motivation_ptr->motivation_id, DOMINO_TASK_TYPE_SLEEP, 80U);
        addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_SLEEP, 600000U, 0U, 0U);
        restoreMotivation(human_id)->round_task_id = task_id;
    }
    human(1U)->data->ai_flags |= DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN;
    dominoHumanSettle(human(1U), 5000U);
    setNowMs(5000U);
    dominoHumanSettle(human(2U), 5000U);
    submitConditionForTest(2U, true);
    const DominoTask* t1_ptr = activeOrHead(1U);
    const DominoTask* t2_ptr = activeOrHead(2U);
    CHECK(t1_ptr->current_behavior_id_list[0] == 0U && t2_ptr->current_behavior_id_list[0] == 0U);
    CHECK(t1_ptr->exec_status == t2_ptr->exec_status && t1_ptr->main_cancelled_count == t2_ptr->main_cancelled_count);
    CHECK(t1_ptr->last_behavior_end_reason == DOMINO_END_REASON_CONDITION_LOST &&
          t2_ptr->last_behavior_end_reason == DOMINO_END_REASON_CONDITION_LOST);
    fixtureEnd();
}

/** @brief 应激（接受插入）：运行中的消息任务遇到联系信息，选择接受插入；行为规划器为它插入辅 1 通话，主行为不受影响。
 * 非法插入（主通道、带依赖、轮次过期）整份拒绝；主行为先完成时任务记下完成条件、等插入行为结束后才提交成功；插入行为不计入成功证明和进度。
 */
static void testStimulusInsert(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("stimulus_insert", 1U, energy_list);
    const domino_motivation_id_t motivation_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
    addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
    const domino_task_id_t task_id = addRunningTask(1U, motivation_id, DOMINO_TASK_TYPE_MESSAGE, 0U);
    addRunningBehavior(task_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_MESSAGE, 4000U, 0U, 0U);
    setNowMs(1000U);
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 77U);
    const DominoHumanData* data_ptr = human(1U)->data;
    CHECK(data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_CONTACT && data_ptr->stimulus_source_id == 77U && data_ptr->stimulus_round == 1U);
    CHECK(dominoHumanStimulusNeed(human(1U)) == DOMINO_HUMAN_STIMULUS_NEED_INSERT);
    const DominoHumanPlanSlot slot = runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR];
    CHECK(slot.request_id != 0U && slot.scope_id == task_id && slot.priority == DOMINO_HUMAN_PLAN_PRIORITY_HIGH);
    /* 登记外部信息同时置动机和任务待规划标志；是否需要回应任务由规划器判断，执行器不检查应激选择。 */
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id != 0U &&
          runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    /* 非法插入：主通道、带依赖、轮次过期。 */
    const DominoTask* task_ptr = dominoGetTaskByID(task_id);
    DominoBehaviorPlanItem step = planStep(DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 8000U);
    DominoBehaviorCandidateEntry entry = behaviorEntry(task_ptr, DOMINO_BEHAVIOR_CANDIDATE_INSERT, 1U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &entry, 1U, &step)));
    step = planStep(DOMINO_BEHAVIOR_CHANNEL_AUX_1, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 8000U);
    step.dependency_list[0] = (DominoBehaviorDependency){.step_seq = dominoGetBehaviorByID(task_ptr->current_behavior_id_list[0])->step_seq,
                                                         .kind = DOMINO_BEHAVIOR_DEPENDENCY_WHILE};
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &entry, 1U, &step)));
    step = planStep(DOMINO_BEHAVIOR_CHANNEL_AUX_1, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 8000U);
    entry.expected_stimulus_round = 0U;
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &entry, 1U, &step)));
    CHECK(data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_CONTACT && dominoGetTaskByID(task_id)->plan_count == 0U);
    /* 合法插入：辅 1 通话 8s，第 1 秒启动并承接外部信息。 */
    entry = behaviorEntry(dominoGetTaskByID(task_id), DOMINO_BEHAVIOR_CANDIDATE_INSERT, 1U);
    CHECK(
        injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id, buildBehaviorCandidate(1U, 1U, &entry, 1U, &step)));
    const DominoBehavior* inserted_ptr = channelBehavior(1U, DOMINO_BEHAVIOR_CHANNEL_AUX_1);
    CHECK(inserted_ptr != nullptr && (inserted_ptr->flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U && inserted_ptr->started_at_ms == 1000U &&
          inserted_ptr->type == DOMINO_BEHAVIOR_TYPE_PHONE_CALL);
    CHECK(data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE && data_ptr->stimulus_round == 1U && data_ptr->active_task_id == task_id);
    CHECK(channelBehavior(1U, DOMINO_BEHAVIOR_CHANNEL_MAIN) != nullptr && runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR].request_id == 0U);
    /* 第 4 秒主行为完成：完成条件成立，但插入行为仍在运行，任务保持 OPEN 等待。 */
    advanceAt(1U, 4000U);
    task_ptr = dominoGetTaskByID(task_id);
    CHECK(task_ptr != nullptr && task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN &&
          (task_ptr->flags & DOMINO_TASK_FLAG_COMPLETION_REACHED) != 0U);
    CHECK(task_ptr->main_succeeded_count == 1U && task_ptr->progress_ms == 4000U && task_ptr->current_behavior_id_list[1] != 0U);
    CHECK(contactMotivation(1U)->success_count == 0U && !dominoHumanTaskNeedsBehaviorPlan(human(1U), task_ptr));
    domino_runtime_ms_t due_ms;
    CHECK(dominoHumanPredictNextCheck(human(1U), &due_ms) && due_ms == 9000U);
    /* 完成条件已成立的任务不再接受替换步骤。 */
    const DominoBehaviorCandidateEntry late_entry = behaviorEntry(task_ptr, DOMINO_BEHAVIOR_CANDIDATE_REPLACE, 1U);
    const DominoBehaviorPlanItem late_step = planStep(DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_MESSAGE, 1000U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &late_entry, 1U, &late_step)));
    CHECK(dominoGetTaskByID(task_id)->plan_count == 0U);
    /* 第 9 秒插入行为结束：此时才提交任务成功，插入行为不计入主行为成功证明和进度。 */
    advanceAt(1U, 9000U);
    CHECK(dominoGetTaskByID(task_id) == nullptr && human(1U)->data->active_task_id == 0U);
    CHECK(contactMotivation(1U)->success_count == 1U && dominoHumanMotivationHistoryAt(contactMotivation(1U), 0U).ended_at_ms == 9000U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 应激（完全让位）：受控回应联系的动机、任务和行为规划                                                                 */
/* ---------------------------------------------------------------------------------------------------------------- */

static uint8_t contactMotivationCompute(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                        uint32_t budget, DominoHumanPlanOutput* output_ptr) {
    if (dominoHumanStimulusNeed(human_ptr) != DOMINO_HUMAN_STIMULUS_NEED_TASK ||
        dominoHumanFindActiveMotivation(human_ptr->data, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT) != nullptr) {
        return dominoHumanPlanMotivations(human_ptr, slot_ptr, now_ms, budget, output_ptr);
    }
    const DominoMotivationCandidateEntry entry = {.op = DOMINO_MOTIVATION_CANDIDATE_CREATE, .type = DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT};
    output_ptr->candidate_ptr = buildMotivationCandidate(1U, 1U, &entry);
    return DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE;
}

/** @brief 为待处理外部信息提出回应通话任务；source_offset 用于构造来源不符的非法建议。 */
static uint32_t g_contact_source_offset;

/** @brief 受控任务规划：有回应联系动机且需要回应任务时，为尚未承接的外部信息提出回应通话；其他情况交给生产策略。 */
static uint8_t contactTaskCompute(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms, uint32_t budget,
                                  DominoHumanPlanOutput* output_ptr) {
    const DominoMotivation* motivation_ptr = dominoHumanFindActiveMotivation(human_ptr->data, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT);
    if (motivation_ptr == nullptr || dominoHumanStimulusNeed(human_ptr) != DOMINO_HUMAN_STIMULUS_NEED_TASK) {
        return dominoHumanPlanTasks(human_ptr, slot_ptr, now_ms, budget, output_ptr);
    }
    const DominoTaskCandidateEntry entry = {
        .motivation_id = motivation_ptr->motivation_id,
        .work_source_id = human_ptr->data->stimulus_source_id + g_contact_source_offset,
        .work_round = human_ptr->data->stimulus_round,
        .op = DOMINO_TASK_CANDIDATE_CREATE,
        .type = DOMINO_TASK_TYPE_PHONE_CALL,
        .insert_index = DOMINO_PLAN_CANDIDATE_INSERT_APPEND,
        .work_source_kind = DOMINO_TASK_SOURCE_STIMULUS,
    };
    output_ptr->candidate_ptr = buildTaskCandidate(1U, 1U, &entry);
    return DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE;
}

/** @brief 回应通话的有效时长；端到端场景放宽以便在停止前观察到运行中的通话。 */
static domino_runtime_ms_t g_contact_call_ms = 3000U;

/** @brief 回应任务：主通道通话，辅 1 记录消息伴随通话；其他任务交给生产策略。 */
static uint8_t contactBehaviorCompute(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms, uint32_t budget,
                                      DominoHumanPlanOutput* output_ptr) {
    const DominoTask* task_ptr = dominoGetTaskByID(slot_ptr->scope_id);
    if (task_ptr == nullptr || task_ptr->type != DOMINO_TASK_TYPE_PHONE_CALL || !dominoHumanTaskNeedsBehaviorPlan(human_ptr, task_ptr)) {
        return dominoHumanPlanBehaviors(human_ptr, slot_ptr, now_ms, budget, output_ptr);
    }
    DominoBehaviorPlanItem step_list[2] = {planStep(DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, g_contact_call_ms),
                                           planStep(DOMINO_BEHAVIOR_CHANNEL_AUX_1, DOMINO_BEHAVIOR_TYPE_MESSAGE, 60000U)};
    step_list[1].dependency_list[0] = localDependency(0U, DOMINO_BEHAVIOR_DEPENDENCY_WHILE);
    const DominoBehaviorCandidateEntry entry = behaviorEntry(task_ptr, DOMINO_BEHAVIOR_CANDIDATE_REPLACE, 2U);
    output_ptr->candidate_ptr = buildBehaviorCandidate(1U, 1U, &entry, 2U, step_list);
    return DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE;
}

/** @brief 应激（完全让位）：睡眠中遇到联系信息，选择完全让位；回应任务规划期间睡眠照常进行，回应任务首批行为就绪时睡眠才让位
 * （主行为取消、进度归并、计划清空、回到队首），通话结束后睡眠沿用原 task_id 和首次开始时刻恢复，替代行为取得新 ID。
 */
static void testStimulusYield(void) {
    const uint8_t energy_list[] = {20U};
    fixtureBegin("stimulus_yield", 1U, energy_list);
    const domino_task_id_t sleep_id = prepareSleepTask(1U, true);
    const domino_behavior_id_t first_sleep_behavior_id = dominoGetTaskByID(sleep_id)->current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_MAIN];
    CHECK(sleep_id != 0U && first_sleep_behavior_id != 0U);
    setNowMs(5000U);
    const DominoHumanRuntime* runtime_ptr = runtimeOf(1U);
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 42U);
    CHECK(dominoHumanStimulusNeed(human(1U)) == DOMINO_HUMAN_STIMULUS_NEED_TASK);
    /* 外部信息同时发起动机和任务规划（以人物为范围）。 */
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id != 0U &&
          runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].scope_kind == DOMINO_HUMAN_PLAN_SCOPE_HUMAN);
    /* 新建回应联系动机：任务请求在途，任务待规划标志保留到结果处理之后。 */
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, contactMotivationCompute) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE);
    CHECK(contactMotivation(1U) != nullptr && (runtime_ptr->pending_mask & DOMINO_HUMAN_PLAN_MASK_TASK) != 0U);
    /* 来源与待处理外部信息不符的回应任务被整份拒绝：设兜底时刻；保留的标志立即再发起一次任务规划。 */
    g_contact_source_offset = 1U;
    serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, contactTaskCompute);
    CHECK(human(1U)->data->task_count == 1U && dominoHumanExecutionGetStats().rejected_candidate_count == 1U);
    CHECK(runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] == 8000U &&
          runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    g_contact_source_offset = 0U;
    serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, contactTaskCompute);
    const DominoHumanData* data_ptr = human(1U)->data;
    CHECK(data_ptr->task_count == 2U && data_ptr->task_queue_count == 1U && data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE);
    CHECK((runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U);
    const domino_task_id_t phone_id = data_ptr->task_queue_list[0];
    const DominoTask* phone_ptr = dominoGetTaskByID(phone_id);
    CHECK(phone_ptr->work_source_kind == DOMINO_TASK_SOURCE_STIMULUS && phone_ptr->work_source_id == 42U && phone_ptr->work_round == 1U &&
          (phone_ptr->flags & DOMINO_TASK_FLAG_PREEMPT) != 0U && phone_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_CONTACT);
    /* 回应任务还没有可启动的行为：睡眠照常运行，不提前让位。 */
    CHECK(data_ptr->active_task_id == sleep_id && channelBehavior(1U, 0U)->behavior_id == first_sleep_behavior_id);
    CHECK(runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR].scope_id == phone_id);
    serveRequest(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, contactBehaviorCompute);
    /* 回应任务首批行为就绪：睡眠完全让位，通话与伴随的消息记录同时开始。 */
    CHECK(data_ptr->active_task_id == phone_id && data_ptr->task_queue_count == 1U && data_ptr->task_queue_list[0] == sleep_id);
    phone_ptr = dominoGetTaskByID(phone_id);
    CHECK(phone_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && phone_ptr->started_at_ms == 5000U &&
          (phone_ptr->flags & DOMINO_TASK_FLAG_PREEMPT) == 0U);
    CHECK(channelBehavior(1U, 0U) != nullptr && channelBehavior(1U, 0U)->type == DOMINO_BEHAVIOR_TYPE_PHONE_CALL);
    CHECK(channelBehavior(1U, 1U) != nullptr && channelBehavior(1U, 1U)->dependency_list[0].kind == DOMINO_BEHAVIOR_DEPENDENCY_WHILE);
    const DominoTask* sleep_ptr = dominoGetTaskByID(sleep_id);
    CHECK(sleep_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && sleep_ptr->started_at_ms == 1000U && sleep_ptr->progress_ms == 4000U);
    CHECK(sleep_ptr->main_cancelled_count == 1U && sleep_ptr->last_behavior_end_reason == DOMINO_END_REASON_YIELDED &&
          sleep_ptr->current_behavior_id_list[0] == 0U && sleep_ptr->plan_count == 0U);
    CHECK(dominoGetBehaviorByID(first_sleep_behavior_id)->status == DOMINO_BEHAVIOR_STATUS_CANCELLED &&
          dominoGetBehaviorByID(first_sleep_behavior_id)->ended_at_ms == 5000U);
    /* 运行任务让位和回应任务启动都不置任务待规划标志。 */
    CHECK(runtime_ptr->pending_mask == 0U && runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    /* 让位的睡眠在队列中等待，可提前规划剩余睡眠，但不会抢回运行。 */
    pumpRequests(1U);
    CHECK(dominoGetTaskByID(sleep_id)->plan_count == 1U && data_ptr->active_task_id == phone_id);
    /* 第 8 秒通话结束：伴随的消息记录随之取消，回应任务成功；睡眠恢复，替代行为取得新 ID。 */
    advanceAt(1U, 8000U);
    CHECK(dominoGetTaskByID(phone_id) == nullptr && contactMotivation(1U)->success_count == 1U);
    CHECK(data_ptr->active_task_id == sleep_id && data_ptr->task_queue_count == 0U);
    const DominoBehavior* resumed_ptr = channelBehavior(1U, DOMINO_BEHAVIOR_CHANNEL_MAIN);
    CHECK(resumed_ptr != nullptr && resumed_ptr->type == DOMINO_BEHAVIOR_TYPE_SLEEP && resumed_ptr->behavior_id > first_sleep_behavior_id &&
          resumed_ptr->started_at_ms == 8000U);
    CHECK(dominoGetTaskByID(sleep_id)->started_at_ms == 1000U && restoreMotivation(1U)->success_count == 0U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 外部信息轮次：没有运行任务时需要回应任务；撤回后不再需要；同类信息未承接时再次到达不开新轮，撤回后再到达开新轮。 */
static void testStimulusRounds(void) {
    const uint8_t energy_list[] = {90U};
    fixtureBegin("stimulus_rounds", 1U, energy_list);
    setNowMs(1000U);
    const DominoHumanData* data_ptr = human(1U)->data;
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 5U);
    CHECK(data_ptr->stimulus_round == 1U && dominoHumanStimulusNeed(human(1U)) == DOMINO_HUMAN_STIMULUS_NEED_TASK);
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 6U);
    CHECK(data_ptr->stimulus_round == 1U && data_ptr->stimulus_source_id == 6U);
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_NONE, 6U);
    CHECK(data_ptr->stimulus_round == 1U && dominoHumanStimulusNeed(human(1U)) == DOMINO_HUMAN_STIMULUS_NEED_NONE);
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 7U);
    CHECK(data_ptr->stimulus_round == 2U && data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_CONTACT);
    fixtureEnd();
}

/** @brief 测试验收专用人物输入：入口拒绝越界精力和不合法的外部信息类别，不投递；串行处理先按旧值结算到本次时刻再写入，
 * 值未变化不改事实，不存在的人物被忽略。
 */
static void testTestInputs(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("test_inputs", 1U, energy_list);
    CHECK(dominoHumanSubmitEnergyForTest(1U, DOMINO_HUMAN_NEED_MAX + 1U) == ERR_INVALID_PARAM);
    CHECK(dominoHumanSubmitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_COUNT, 1U) == ERR_INVALID_PARAM);
    const DominoHumanData* data_ptr = human(1U)->data;
    setNowMs(30000U);
    submitEnergyForTest(1U, 30U);
    CHECK(data_ptr->energy == 30U && data_ptr->settled_at_ms == 30000U);
    submitConditionForTest(1U, true);
    CHECK((data_ptr->ai_flags & DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN) != 0U);
    submitConditionForTest(1U, true);
    CHECK((data_ptr->ai_flags & DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN) != 0U);
    submitConditionForTest(1U, false);
    CHECK((data_ptr->ai_flags & DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN) == 0U);
    submitEnergyForTest(9U, 10U);
    CHECK(dominoGetHumanByID(9U) == nullptr && data_ptr->energy == 30U);
    fixtureEnd();
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 规划发起、下次规划时刻与规划结果复核                                                                               */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 动机规划的发起：加载、外部信息、下次规划时刻到达和任务结束分别发起；人物没有有效动机本身不发起，规划器不建动机并返回
 * NO_CHANGE 后不出现忙循环；请求在途期间新置位的待评估标志保留，结果处理后再发起一次请求。
 */
static void testMotivationTriggers(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("motivation_triggers", 1U, energy_list);
    const DominoHumanRuntime* runtime_ptr = runtimeOf(1U);
    /* 加载：InitAfter 为每个人物置动机待评估标志；没有有效动机的人物不置任务待规划标志。 */
    CHECK(runtime_ptr->pending_mask == DOMINO_HUMAN_PLAN_MASK_MOTIVATION && runtime_ptr->next_plan_mask == 0U);
    setNowMs(1000U);
    dominoHumanExecutionAdvanceForTest(1U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id != 0U && runtime_ptr->pending_mask == 0U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    /* 规划器选择不建动机：NO_CHANGE，下次规划时刻为“无”。 */
    serveControlled(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE, nullptr, false, 0U);
    CHECK(restoreMotivation(1U) == nullptr && runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id == 0U);
    /* 人物没有有效动机本身不发起：其后的推进都不再投递，也没有需要登记的唤醒。 */
    const uint64_t posted_count = dominoHumanExecutionGetStats().posted_request_count_list[DOMINO_HUMAN_PLANNER_MOTIVATION];
    advanceAt(1U, 5000U);
    advanceAt(1U, 65000U);
    CHECK(dominoHumanExecutionGetStats().posted_request_count_list[DOMINO_HUMAN_PLANNER_MOTIVATION] == posted_count);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id == 0U && runtime_ptr->wakeup_game_timer_id == 0U);
    /* 外部信息：发起动机规划，同时发起任务规划。 */
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 9U);
    const uint64_t first_id = runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id;
    CHECK(first_id != 0U && runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    /* 请求在途期间撤回外部信息：标志保留，结果处理后再发起一次请求。 */
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_NONE, 9U);
    CHECK((runtime_ptr->pending_mask & DOMINO_HUMAN_PLAN_MASK_MOTIVATION) != 0U);
    serveControlled(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE, nullptr, false, 0U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id > first_id &&
          (runtime_ptr->pending_mask & DOMINO_HUMAN_PLAN_MASK_MOTIVATION) == 0U);
    /* 规划器要求第 90 秒再看：结果覆盖下次规划时刻，唤醒登记该时刻。 */
    serveControlled(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE, nullptr, true, 90000U);
    for (uint32_t round = 0U; round < 4U && runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U; round++) {
        serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr);
    }
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id == 0U &&
          runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    CHECK(runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_MOTIVATION] == 90000U && runtime_ptr->wakeup_due_ms == 90000U);
    /* 下次规划时刻到达：清除时刻、置待评估标志并发起动机规划。 */
    advanceAt(1U, 90000U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id != 0U &&
          (runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_MOTIVATION) == 0U);
    /* 生产策略新建恢复精力动机；动机集合变化随即发起任务规划。 */
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE && restoreMotivation(1U) != nullptr);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE);
    /* 任务结束：未启动的睡眠任务被撤销，发起动机规划。 */
    const DominoTaskCandidateEntry sleep_entry = sleepEntry(restoreMotivation(1U)->motivation_id, 0U, 1U);
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &sleep_entry)));
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id == 0U);
    submitEnergyForTest(1U, 95U);
    CHECK(human(1U)->data->task_count == 0U && runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_MOTIVATION].request_id != 0U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 动机终结：终态不合法时整份拒绝并设兜底时刻；规划之后动机下又接受了新任务，终结仍照常提交；终结后挂在该动机下的新任务被拒绝，
 * 未启动的关联任务撤销并回收，已启动的任务继续执行、成功仍给原动机计次但动机不恢复有效；重复终结和目标已回收时忽略；
 * 回收最后一个关联任务时、提交终结时没有关联任务时两个回收时机；终结后由新 ID 新建同类动机。
 */
static void testMotivationTermination(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("motivation_termination", 1U, energy_list);
    setNowMs(1000U);
    dominoHumanSettle(human(1U), 1000U);
    const DominoHumanRuntime* runtime_ptr = runtimeOf(1U);
    const domino_motivation_id_t restore_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY)->motivation_id;
    const domino_motivation_id_t contact_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
    /* 已启动的任务：主通道通话 4s，第 5 秒成功。 */
    const domino_task_id_t started_id = addRunningTask(1U, contact_id, DOMINO_TASK_TYPE_PHONE_CALL, 0U);
    addRunningBehavior(started_id, DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 4000U, 0U, 0U);
    /* 动机规划器读取快照之后，该动机下又接受了一项未启动的任务。 */
    const DominoTaskCandidateEntry queued_entry = phoneEntry(contact_id, 7U);
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &queued_entry)));
    const domino_task_id_t queued_id = human(1U)->data->task_queue_list[0];
    CHECK(dominoGetTaskByID(queued_id)->exec_status == DOMINO_TASK_EXEC_STATUS_NONE);
    /* 终态不合法：整份拒绝，下次规划时刻设为处理时刻 + retry_delay_ms。 */
    DominoMotivationCandidateEntry terminate_entry = {
        .motivation_id = contact_id, .op = DOMINO_MOTIVATION_CANDIDATE_TERMINATE, .terminal_status = DOMINO_MOTIVATION_STATUS_ACTIVE};
    CHECK(
        !injectCandidate(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildMotivationCandidate(1U, 1U, &terminate_entry)));
    CHECK(contactMotivation(1U) != nullptr && runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_MOTIVATION] == 4000U);
    /* 正常终结：写入终态；未启动的任务撤销并回收；已启动的任务继续运行；动机集合变化发起任务规划。 */
    terminate_entry.terminal_status = DOMINO_MOTIVATION_STATUS_ACHIEVED;
    CHECK(
        injectCandidate(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildMotivationCandidate(1U, 1U, &terminate_entry)));
    const DominoMotivation* contact_ptr = dominoGetMotivationByID(contact_id);
    CHECK(contact_ptr != nullptr && contact_ptr->status == DOMINO_MOTIVATION_STATUS_ACHIEVED && contactMotivation(1U) == nullptr);
    CHECK(contact_ptr->last_failure_reason == DOMINO_END_REASON_MOTIVATION_ENDED && contact_ptr->success_count == 0U);
    CHECK(dominoGetTaskByID(queued_id) == nullptr && human(1U)->data->task_queue_count == 0U);
    CHECK(dominoGetTaskByID(started_id)->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && human(1U)->data->active_task_id == started_id);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U &&
          (runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_MOTIVATION) == 0U);
    /* 挂在已终结动机下的新任务被拒绝。 */
    const DominoTaskCandidateEntry late_entry = phoneEntry(contact_id, 8U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &late_entry)));
    CHECK(human(1U)->data->task_count == 1U);
    /* 重复终结：忽略该条目，结果仍被接受，终态不变。 */
    terminate_entry.terminal_status = DOMINO_MOTIVATION_STATUS_FAILED;
    CHECK(
        injectCandidate(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildMotivationCandidate(1U, 1U, &terminate_entry)));
    CHECK(dominoGetMotivationByID(contact_id)->status == DOMINO_MOTIVATION_STATUS_ACHIEVED);
    /* 已启动的任务继续执行到第 5 秒成功：给原动机计次并追加明细，动机仍为终态。 */
    dominoHumanSettle(human(1U), 5000U);
    contact_ptr = dominoGetMotivationByID(contact_id);
    CHECK(contact_ptr != nullptr && contact_ptr->success_count == 1U && contact_ptr->status == DOMINO_MOTIVATION_STATUS_ACHIEVED);
    CHECK(contact_ptr->history_count == 1U && dominoHumanMotivationHistoryAt(contact_ptr, 0U).task_id == started_id);
    /* 回收最后一个关联任务时一并回收已终结的动机。 */
    advanceAt(1U, 5000U);
    CHECK(dominoGetTaskByID(started_id) == nullptr && dominoGetMotivationByID(contact_id) == nullptr && human(1U)->data->motivation_count == 1U);
    /* 目标已回收：忽略。 */
    terminate_entry.terminal_status = DOMINO_MOTIVATION_STATUS_ACHIEVED;
    CHECK(
        injectCandidate(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildMotivationCandidate(1U, 1U, &terminate_entry)));
    CHECK(human(1U)->data->motivation_count == 1U);
    /* 提交终结时没有关联任务：同一次处理内立即回收。 */
    const DominoMotivationCandidateEntry revoke_entry = {
        .motivation_id = restore_id, .op = DOMINO_MOTIVATION_CANDIDATE_TERMINATE, .terminal_status = DOMINO_MOTIVATION_STATUS_REVOKED};
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildMotivationCandidate(1U, 1U, &revoke_entry)));
    CHECK(dominoGetMotivationByID(restore_id) == nullptr && human(1U)->data->motivation_count == 0U);
    /* 又出现同类需求时，由动机规划器以新 ID 新建，不恢复旧动机。 */
    issueSlot(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE);
    CHECK(restoreMotivation(1U) != nullptr && restoreMotivation(1U)->motivation_id > contact_id);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 任务规划的发起：请求以人物为范围；动机集合变化、人物输入变化和下次规划时刻到达分别发起；规划器不建任务时返回 NO_CHANGE
 * 并给出精力降到触发值的时刻；本轮睡眠未结束时不给时刻；请求在途期间新置位的标志保留；运行任务启动不发起。
 */
static void testTaskTriggers(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("task_triggers", 1U, energy_list);
    const DominoHumanRuntime* runtime_ptr = runtimeOf(1U);
    setNowMs(1000U);
    dominoHumanExecutionAdvanceForTest(1U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    serveRequest(1U, DOMINO_HUMAN_PLANNER_MOTIVATION, nullptr);
    /* 动机集合变化：以人物为范围发起任务规划，不绑定单个动机。 */
    const DominoHumanPlanSlot task_slot = runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK];
    CHECK(task_slot.request_id != 0U && task_slot.scope_kind == DOMINO_HUMAN_PLAN_SCOPE_HUMAN && task_slot.scope_id == 0U);
    /* 精力 50 高于触发值：不建任务，下次规划时刻为第 20 次整分钟衰减的第 1200 秒。 */
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE && human(1U)->data->task_count == 0U);
    CHECK(runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] == 1200000U && runtime_ptr->wakeup_due_ms == 1200000U);
    CHECK(runtime_ptr->pending_mask == 0U);
    /* 人物输入变化：精力改为 40，重新规划并给出新的时刻（第 600 秒）。 */
    setNowMs(2000U);
    submitEnergyForTest(1U, 40U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr);
    CHECK(runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] == 600000U && runtime_ptr->wakeup_due_ms == 600000U);
    /* 睡眠条件变化同样发起。 */
    submitConditionForTest(1U, true);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U);
    serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr);
    submitConditionForTest(1U, false);
    serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr);
    /* 下次规划时刻到达：精力恰好降到触发值，提出睡眠任务；本轮未结束，不给时刻。 */
    advanceAt(1U, 600000U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U && (runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U);
    CHECK(human(1U)->data->energy == 30U);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE && human(1U)->data->task_count == 1U);
    CHECK((runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U);
    /* 请求在途期间两次精力输入：只在途一个请求，标志保留，结果处理后再发起一次；本轮未结束，NO_CHANGE。 */
    submitEnergyForTest(1U, 29U);
    const uint64_t first_id = runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id;
    submitEnergyForTest(1U, 28U);
    CHECK(first_id != 0U && runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == first_id &&
          (runtime_ptr->pending_mask & DOMINO_HUMAN_PLAN_MASK_TASK) != 0U);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE &&
          runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id > first_id);
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_TASK, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE &&
          runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U && (runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U);
    /* 运行任务启动不发起任务规划。 */
    CHECK(serveRequest(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, nullptr) == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE && human(1U)->data->active_task_id != 0U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U && (runtime_ptr->pending_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U);
    fixtureEnd();
}

/** @brief 下次规划时刻：只有动机时预测不给出复评时刻；NO_PLAN 取结果给出的时刻，“无”时清除，只有可选条目被省略时取结果给出的值；
 * 指定插入位置的候选因运行任务启动被整份拒绝后设兜底时刻、到点再规划；兜底等待期内登记外部信息立即发起；唤醒只登记一个最早时刻。
 */
static void testNextPlanTimes(void) {
    const uint8_t energy_list[] = {60U};
    fixtureBegin("next_plan_times", 1U, energy_list);
    setNowMs(1000U);
    dominoHumanSettle(human(1U), 1000U);
    const DominoHumanRuntime* runtime_ptr = runtimeOf(1U);
    const domino_motivation_id_t contact_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
    /* 只有动机、没有任务：只读预测不再给出动机复评时刻。 */
    domino_runtime_ms_t due_ms;
    CHECK(!dominoHumanPredictNextCheck(human(1U), &due_ms));
    /* NO_PLAN 取结果给出的时刻，唤醒登记该时刻。 */
    issueSlot(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U);
    serveControlled(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN, nullptr, true, 4000U);
    CHECK(runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] == 4000U && runtime_ptr->wakeup_due_ms == 4000U);
    /* 结果为“无”时清除，人物不再登记唤醒。 */
    issueSlot(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U);
    serveControlled(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE, nullptr, false, 0U);
    CHECK((runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U && runtime_ptr->wakeup_game_timer_id == 0U);
    /* 只有可选条目被省略：不算整份拒绝，取结果给出的值。 */
    DominoTaskCandidateEntry bad_entry = phoneEntry(contact_id, 1U);
    bad_entry.type = DOMINO_TASK_TYPE_NONE;
    const DominoTaskCandidateEntry optional_list[] = {phoneEntry(contact_id, 1U), bad_entry};
    issueSlot(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U);
    CHECK(
        serveControlled(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE, buildTaskCandidate(2U, 1U, optional_list), true, 50000U));
    CHECK(runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] == 50000U && human(1U)->data->task_queue_count == 1U);
    /* 规划器按当前队列版本提出插队，之后运行任务启动改变了队列版本；启动不置任务待规划标志。 */
    const domino_task_id_t first_id = human(1U)->data->task_queue_list[0];
    DominoTaskCandidateEntry insert_entry = phoneEntry(contact_id, 2U);
    insert_entry.insert_index = 0U;
    insert_entry.expected_queue_version = runtime_ptr->queue_version;
    void* stale_ptr = buildTaskCandidate(1U, 1U, &insert_entry);
    const DominoBehaviorPlanItem call_step = planStep(DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 10000U);
    const DominoBehaviorCandidateEntry call_entry = behaviorEntry(dominoGetTaskByID(first_id), DOMINO_BEHAVIOR_CANDIDATE_REPLACE, 1U);
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, first_id,
                          buildBehaviorCandidate(1U, 1U, &call_entry, 1U, &call_step)));
    CHECK(human(1U)->data->active_task_id == first_id && (runtime_ptr->pending_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    /* 插队候选因队列版本不符被整份拒绝：设兜底时刻（第 4 秒），没有其他事件也会再规划。 */
    issueSlot(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U);
    CHECK(!serveControlled(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE, stale_ptr, false, 0U));
    CHECK(runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] == 4000U &&
          runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    CHECK(runtime_ptr->wakeup_due_ms == 4000U);
    advanceAt(1U, 4000U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U && (runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) == 0U);
    serveControlled(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE, nullptr, false, 0U);
    /* 兜底等待期内登记外部信息：立即发起任务规划，不等兜底时刻；兜底时刻保留到结果处理时覆盖。 */
    issueSlot(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U);
    CHECK(!serveControlled(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE, buildTaskCandidate(1U, 1U, &bad_entry), false, 0U));
    CHECK(runtime_ptr->next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_TASK] == 7000U &&
          runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id == 0U);
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 3U);
    CHECK(runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_TASK].request_id != 0U && (runtime_ptr->next_plan_mask & DOMINO_HUMAN_PLAN_MASK_TASK) != 0U);
    /* 唤醒只登记一个最早时刻：兜底时刻（第 7 秒）早于通话结束（第 11 秒）。 */
    CHECK(runtime_ptr->wakeup_due_ms == 7000U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 行为结果的复核：NO_CHANGE 和 NO_PLAN 后任务仍需行为规划时设重试；规划之后执行器启动并消费了步骤，旧的替换方案因计划版本不符被拒绝；
 * 声明依赖的当前行为已变化时拒绝；INSERT 不复核应激选择，睡眠选择完全让位时插入仍照常提交；外部信息已撤回、轮次已变化、任务已不是运行任务时拒绝；
 * 回应任务与插入步骤只有先提交的一方承接外部信息；接受后任务仍需行为规划时设重试。
 */
static void testBehaviorReview(void) {
    const uint8_t energy_list[] = {50U, 20U};
    fixtureBegin("behavior_review", 2U, energy_list);
    setNowMs(1000U);
    dominoHumanSettle(human(1U), 1000U);
    const domino_motivation_id_t contact_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
    const domino_task_id_t task_id = addRunningTask(1U, contact_id, DOMINO_TASK_TYPE_MESSAGE, 0U);
    /* 运行任务缺少安排：NO_CHANGE 后仍需行为规划，设 3000ms 重试；NO_PLAN 同样设重试。 */
    issueSlot(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id);
    serveControlled(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE, nullptr, false, 0U);
    DominoTask* task_ptr = dominoGetTaskByID(task_id);
    CHECK((task_ptr->flags & DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID) != 0U && task_ptr->behavior_retry_at_ms == 4000U);
    task_ptr->flags = (uint8_t)(task_ptr->flags & ~DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID);
    issueSlot(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id);
    serveControlled(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN, nullptr, false, 0U);
    CHECK((dominoGetTaskByID(task_id)->flags & DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID) != 0U);
    /* 规划器读取计划版本之后，执行器启动并消费了步骤：旧的替换方案被拒绝，已经开始的步骤不会再安排一次。 */
    const DominoBehaviorCandidateEntry stale_entry = behaviorEntry(dominoGetTaskByID(task_id), DOMINO_BEHAVIOR_CANDIDATE_REPLACE, 1U);
    const DominoBehaviorPlanItem work_step = planStep(DOMINO_BEHAVIOR_CHANNEL_MAIN, DOMINO_BEHAVIOR_TYPE_WORK, 5000U);
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                          buildBehaviorCandidate(1U, 1U, &stale_entry, 1U, &work_step)));
    task_ptr = dominoGetTaskByID(task_id);
    const domino_behavior_id_t work_id = task_ptr->current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_MAIN];
    CHECK(work_id != 0U && task_ptr->plan_count == 0U && (task_ptr->flags & DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID) == 0U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &stale_entry, 1U, &work_step)));
    task_ptr = dominoGetTaskByID(task_id);
    CHECK(task_ptr->plan_count == 0U && task_ptr->current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_MAIN] == work_id && task_ptr->last_step_seq == 1U);
    CHECK((task_ptr->flags & DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID) != 0U);
    /* 声明依赖当前行为而当前行为已变化：拒绝。 */
    DominoBehaviorCandidateEntry moved_entry = behaviorEntry(task_ptr, DOMINO_BEHAVIOR_CANDIDATE_REPLACE, 1U);
    moved_entry.expected_current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_MAIN] = work_id + 100U;
    const DominoBehaviorPlanItem note_step = planStep(DOMINO_BEHAVIOR_CHANNEL_AUX_1, DOMINO_BEHAVIOR_TYPE_MESSAGE, 1000U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &moved_entry, 1U, &note_step)));
    CHECK(dominoGetTaskByID(task_id)->plan_count == 0U);
    /* 运行任务接受插入时登记联系信息；另一项排队任务不是运行任务，为它插入被拒绝。 */
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 8U);
    const DominoTaskCandidateEntry queued_entry = phoneEntry(contact_id, 3U);
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &queued_entry)));
    const domino_task_id_t queued_id = human(1U)->data->task_queue_list[0];
    const DominoBehaviorPlanItem chat_step = planStep(DOMINO_BEHAVIOR_CHANNEL_AUX_1, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 2000U);
    DominoBehaviorCandidateEntry insert_entry = behaviorEntry(dominoGetTaskByID(queued_id), DOMINO_BEHAVIOR_CANDIDATE_INSERT, 1U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, queued_id,
                           buildBehaviorCandidate(1U, 1U, &insert_entry, 1U, &chat_step)));
    /* 回应任务先提交：不复核运行任务的应激选择（消息任务选择接受插入），回应任务照常入队并承接外部信息；随后的插入被拒绝。 */
    const DominoTaskCandidateEntry respond_entry = {
        .motivation_id = contact_id,
        .work_source_id = 8U,
        .work_round = human(1U)->data->stimulus_round,
        .op = DOMINO_TASK_CANDIDATE_CREATE,
        .type = DOMINO_TASK_TYPE_PHONE_CALL,
        .insert_index = DOMINO_PLAN_CANDIDATE_INSERT_APPEND,
        .work_source_kind = DOMINO_TASK_SOURCE_STIMULUS,
    };
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &respond_entry)));
    const DominoTask* respond_ptr = dominoGetTaskByID(human(1U)->data->task_queue_list[0]);
    CHECK(human(1U)->data->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE && respond_ptr->work_source_kind == DOMINO_TASK_SOURCE_STIMULUS &&
          (respond_ptr->flags & DOMINO_TASK_FLAG_PREEMPT) != 0U);
    insert_entry = behaviorEntry(dominoGetTaskByID(task_id), DOMINO_BEHAVIOR_CANDIDATE_INSERT, 1U);
    CHECK(!injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_id,
                           buildBehaviorCandidate(1U, 1U, &insert_entry, 1U, &chat_step)));
    /* 接受后任务仍需行为规划：显式清空排队任务的步骤，计划缺口仍在，设重试。 */
    const DominoBehaviorCandidateEntry clear_entry = behaviorEntry(dominoGetTaskByID(queued_id), DOMINO_BEHAVIOR_CANDIDATE_REPLACE, 0U);
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, queued_id,
                          buildBehaviorCandidate(1U, 1U, &clear_entry, 0U, &chat_step)));
    CHECK((dominoGetTaskByID(queued_id)->flags & DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID) != 0U);

    /* 人物 2：睡眠中遇到联系信息，睡眠的应激选择是完全让位。 */
    const domino_task_id_t sleep_id = prepareSleepTask(2U, true);
    submitStimulusForTest(2U, DOMINO_HUMAN_STIMULUS_CONTACT, 5U);
    CHECK(dominoHumanStimulusNeed(human(2U)) == DOMINO_HUMAN_STIMULUS_NEED_TASK);
    /* 外部信息已撤回：插入被拒绝。 */
    submitStimulusForTest(2U, DOMINO_HUMAN_STIMULUS_NONE, 5U);
    DominoBehaviorCandidateEntry sleep_insert = behaviorEntry(dominoGetTaskByID(sleep_id), DOMINO_BEHAVIOR_CANDIDATE_INSERT, 1U);
    CHECK(!injectCandidate(2U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, sleep_id,
                           buildBehaviorCandidate(1U, 1U, &sleep_insert, 1U, &chat_step)));
    /* 新一轮外部信息到达：按旧轮次读取的插入被拒绝。 */
    submitStimulusForTest(2U, DOMINO_HUMAN_STIMULUS_CONTACT, 6U);
    CHECK(human(2U)->data->stimulus_round == 2U);
    CHECK(!injectCandidate(2U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, sleep_id,
                           buildBehaviorCandidate(1U, 1U, &sleep_insert, 1U, &chat_step)));
    /* 不复核应激选择：睡眠选择完全让位，插入步骤仍照常提交并承接外部信息，睡眠继续运行。 */
    sleep_insert = behaviorEntry(dominoGetTaskByID(sleep_id), DOMINO_BEHAVIOR_CANDIDATE_INSERT, 1U);
    CHECK(injectCandidate(2U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_SCOPE_TASK, sleep_id,
                          buildBehaviorCandidate(1U, 1U, &sleep_insert, 1U, &chat_step)));
    const DominoBehavior* inserted_ptr = channelBehavior(2U, DOMINO_BEHAVIOR_CHANNEL_AUX_1);
    CHECK(human(2U)->data->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE && inserted_ptr != nullptr &&
          (inserted_ptr->flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U);
    CHECK(human(2U)->data->active_task_id == sleep_id && channelBehavior(2U, DOMINO_BEHAVIOR_CHANNEL_MAIN) != nullptr);
    /* 插入先提交：此后为同一外部信息提出的回应任务被拒绝。 */
    const DominoTaskCandidateEntry late_respond = {
        .motivation_id = restoreMotivation(2U)->motivation_id,
        .work_source_id = 6U,
        .work_round = 2U,
        .op = DOMINO_TASK_CANDIDATE_CREATE,
        .type = DOMINO_TASK_TYPE_PHONE_CALL,
        .insert_index = DOMINO_PLAN_CANDIDATE_INSERT_APPEND,
        .work_source_kind = DOMINO_TASK_SOURCE_STIMULUS,
    };
    CHECK(!injectCandidate(2U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &late_respond)));
    CHECK(human(2U)->data->task_count == 1U);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/** @brief 行为规划只看运行任务和待执行队首：先运行任务，再为队首提前规划；队首带让位请求时先看队首；
 * 队首之后的任务不被询问，成为队首后才规划。
 */
static void testBehaviorScope(void) {
    const uint8_t energy_list[] = {50U};
    fixtureBegin("behavior_scope", 1U, energy_list);
    setNowMs(1000U);
    dominoHumanSettle(human(1U), 1000U);
    const domino_motivation_id_t contact_id = addMotivation(1U, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT)->motivation_id;
    const domino_task_id_t active_id = addRunningTask(1U, contact_id, DOMINO_TASK_TYPE_MESSAGE, 0U);
    const DominoTaskCandidateEntry queued_list[] = {phoneEntry(contact_id, 10U), phoneEntry(contact_id, 11U)};
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(2U, 2U, queued_list)));
    const domino_task_id_t head_id = human(1U)->data->task_queue_list[0];
    const domino_task_id_t second_id = human(1U)->data->task_queue_list[1];
    CHECK(human(1U)->data->task_queue_count == 2U && human(1U)->data->active_task_id == active_id);
    CHECK(dominoHumanTaskNeedsBehaviorPlan(human(1U), dominoGetTaskByID(active_id)) &&
          dominoHumanTaskNeedsBehaviorPlan(human(1U), dominoGetTaskByID(head_id)) &&
          dominoHumanTaskNeedsBehaviorPlan(human(1U), dominoGetTaskByID(second_id)));
    /* 三项任务都缺计划：先问运行任务（修复使用高优先级）。 */
    const DominoHumanPlanSlot* slot_ptr = &runtimeOf(1U)->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR];
    CHECK(slot_ptr->request_id != 0U && slot_ptr->scope_id == active_id && slot_ptr->priority == DOMINO_HUMAN_PLAN_PRIORITY_HIGH);
    /* 运行任务无方案进入重试等待：为队首提前规划。 */
    serveControlled(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN, nullptr, false, 0U);
    CHECK(slot_ptr->request_id != 0U && slot_ptr->scope_id == head_id && slot_ptr->priority == DOMINO_HUMAN_PLAN_PRIORITY_NORMAL);
    /* 队首也无方案：两者都在等待重试，队首之后的任务不被询问；唤醒取两者的重试时刻。 */
    serveControlled(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN, nullptr, false, 0U);
    CHECK(slot_ptr->request_id == 0U && (dominoGetTaskByID(second_id)->flags & DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID) == 0U);
    CHECK(runtimeOf(1U)->wakeup_game_timer_id != 0U && runtimeOf(1U)->wakeup_due_ms <= 4000U);
    /* 撤销未启动的队首：原第二项成为队首，随即得到行为规划。 */
    const DominoTaskCandidateEntry cancel_entry = {.task_id = head_id, .op = DOMINO_TASK_CANDIDATE_CANCEL};
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &cancel_entry)));
    CHECK(dominoGetTaskByID(head_id) == nullptr && human(1U)->data->task_queue_list[0] == second_id);
    CHECK(slot_ptr->request_id != 0U && slot_ptr->scope_id == second_id);
    serveControlled(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN, nullptr, false, 0U);
    CHECK(slot_ptr->request_id == 0U);
    /* 让位阶段：运行任务结束重试等待、仍缺计划，回应任务进入队首并带让位请求，先问队首的回应任务。 */
    submitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 8U);
    CHECK(slot_ptr->request_id == 0U);
    DominoTask* active_ptr = dominoGetTaskByID(active_id);
    active_ptr->flags = (uint8_t)(active_ptr->flags & ~DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID);
    const DominoTaskCandidateEntry respond_entry = {
        .motivation_id = contact_id,
        .work_source_id = 8U,
        .work_round = human(1U)->data->stimulus_round,
        .op = DOMINO_TASK_CANDIDATE_CREATE,
        .type = DOMINO_TASK_TYPE_PHONE_CALL,
        .insert_index = DOMINO_PLAN_CANDIDATE_INSERT_APPEND,
        .work_source_kind = DOMINO_TASK_SOURCE_STIMULUS,
    };
    CHECK(injectCandidate(1U, DOMINO_HUMAN_PLANNER_TASK, DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U, buildTaskCandidate(1U, 1U, &respond_entry)));
    const domino_task_id_t respond_id = human(1U)->data->task_queue_list[0];
    CHECK((dominoGetTaskByID(respond_id)->flags & DOMINO_TASK_FLAG_PREEMPT) != 0U &&
          dominoHumanTaskNeedsBehaviorPlan(human(1U), dominoGetTaskByID(active_id)));
    CHECK(slot_ptr->request_id != 0U && slot_ptr->scope_id == respond_id);
    /* 回应任务无方案进入重试等待：回到运行任务。 */
    serveControlled(1U, DOMINO_HUMAN_PLANNER_BEHAVIOR, DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN, nullptr, false, 0U);
    CHECK(slot_ptr->request_id != 0U && slot_ptr->scope_id == active_id && slot_ptr->priority == DOMINO_HUMAN_PLAN_PRIORITY_HIGH);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    fixtureEnd();
}

/* ================================================================================================================ */
/* 端到端：真实线程、客户端收发通道骨架与存档延续                                                                     */
/* ================================================================================================================ */

static void sleepMs(uint32_t ms) {
    const struct timespec duration = {.tv_sec = (time_t)(ms / 1000U), .tv_nsec = (long)(ms % 1000U) * 1000000L};
    thrd_sleep(&duration, nullptr);
}

/** @brief 运行中观察的一致性断言：只统计失败，避免轮询中的每次读取都计入检查数；场景结束时统一 CHECK 为 0。 */
static uint32_t g_view_failure_count;

#define EXPECT_VIEW(condition)                                                                     \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            g_view_failure_count++;                                                                \
            DOMINO_PRINT_MESSAGE("VIEW FAIL [%s] line %d: %s", g_case_name, __LINE__, #condition); \
        }                                                                                          \
    } while (0)

/** @brief 单通道摘要：运行任务在该通道的当前行为；0 表示没有。 */
typedef struct NpcChannelView {
    uint32_t behavior_id;
    uint8_t behavior_type;
    uint8_t behavior_status;
} NpcChannelView;

/** @brief NPC 概况：测试线程在一次 human 读锁内复制人物、恢复精力动机、运行任务及各通道当前行为，是同一时刻的视图。
 * @note 仅供验证观察，直接使用 human 模块内部查询，不经客户端通道；引擎不向客户端提供通用的内部数据读取。
 */
typedef struct NpcView {
    uint32_t restore_success_count;  ///< 恢复精力动机的累计成功实施次数。
    uint32_t active_task_id;
    NpcChannelView channel_list[DOMINO_BEHAVIOR_CHANNEL_COUNT];
    bool found_flag;
    uint8_t energy;
    uint8_t state;
    uint8_t sleep_forbidden;
    uint8_t motivation_count;
    uint8_t queue_count;
    uint8_t active_task_type;
    uint8_t active_task_exec_status;
} NpcView;

static NpcView queryNpc(domino_human_id_t human_id) {
    NpcView view = {0};
    dominoHumanReadLock();
    const DominoHuman* human_ptr = dominoGetHumanByID(human_id);
    if (human_ptr != nullptr) {
        const DominoHumanData* data_ptr = human_ptr->data;
        view.found_flag = true;
        view.energy = data_ptr->energy;
        view.state = human_ptr->state;
        view.sleep_forbidden = (data_ptr->ai_flags & DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN) != 0U;
        view.motivation_count = data_ptr->motivation_count;
        view.active_task_id = data_ptr->active_task_id;
        view.queue_count = data_ptr->task_queue_count;
        for (uint8_t index = 0U; index < data_ptr->motivation_count; index++) {
            const DominoMotivation* motivation_ptr = dominoGetMotivationByID(data_ptr->motivation_id_list[index]);
            if (motivation_ptr != nullptr && motivation_ptr->type == DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY) {
                view.restore_success_count = motivation_ptr->success_count;
            }
        }
        const DominoTask* task_ptr = dominoHumanActiveTask(data_ptr);
        EXPECT_VIEW((task_ptr == nullptr) == (view.active_task_id == 0U));
        if (task_ptr != nullptr) {
            view.active_task_type = task_ptr->type;
            view.active_task_exec_status = task_ptr->exec_status;
            for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
                NpcChannelView* channel_ptr = &view.channel_list[channel];
                channel_ptr->behavior_id = task_ptr->current_behavior_id_list[channel];
                if (channel_ptr->behavior_id == 0U) {
                    continue;
                }
                /* 当前行为必有执行记录，且只能是未终结状态。 */
                const DominoBehavior* behavior_ptr = dominoGetBehaviorByID(channel_ptr->behavior_id);
                EXPECT_VIEW(behavior_ptr != nullptr);
                if (behavior_ptr != nullptr) {
                    channel_ptr->behavior_type = behavior_ptr->type;
                    channel_ptr->behavior_status = behavior_ptr->status;
                    EXPECT_VIEW(behavior_ptr->status != DOMINO_BEHAVIOR_STATUS_NONE && behavior_ptr->status < DOMINO_BEHAVIOR_STATUS_SUCCEEDED);
                }
            }
        }
    }
    dominoHumanReadUnlock();
    return view;
}

typedef bool (*NpcPredicate)(const NpcView* view_ptr, uintptr_t arg);

/** @brief 轮询概况直到条件成立或超时。 */
static bool pollNpc(uint32_t human_id, NpcPredicate predicate, uintptr_t arg, uint32_t timeout_ms, NpcView* last_ptr) {
    for (uint32_t waited_ms = 0U; waited_ms <= timeout_ms; waited_ms += 50U) {
        *last_ptr = queryNpc(human_id);
        if (predicate(last_ptr, arg)) {
            return true;
        }
        sleepMs(50U);
    }
    return false;
}

static bool sleepBehaviorActive(const NpcView* view_ptr, uintptr_t not_behavior_id) {
    const NpcChannelView* channel_ptr = &view_ptr->channel_list[DOMINO_BEHAVIOR_CHANNEL_MAIN];
    return channel_ptr->behavior_type == DOMINO_BEHAVIOR_TYPE_SLEEP && channel_ptr->behavior_status == DOMINO_BEHAVIOR_STATUS_ACTIVE &&
           channel_ptr->behavior_id != (uint32_t)not_behavior_id;
}

/** @brief 条件失效与主行为停止在同一次人物读取中可见：任务仍是运行任务，但已没有主行为。 */
static bool sleepBehaviorStopped(const NpcView* view_ptr, uintptr_t arg) {
    (void)arg;
    return view_ptr->sleep_forbidden == 1U && view_ptr->active_task_id != 0U &&
           view_ptr->channel_list[DOMINO_BEHAVIOR_CHANNEL_MAIN].behavior_id == 0U;
}

static bool successCountReached(const NpcView* view_ptr, uintptr_t count) {
    return view_ptr->restore_success_count >= (uint32_t)count;
}

/** @brief 回应通话已开始：主通道通话与伴随的消息记录并行，睡眠已让位排队。 */
static bool respondRunning(const NpcView* view_ptr, uintptr_t arg) {
    (void)arg;
    return view_ptr->active_task_type == DOMINO_TASK_TYPE_PHONE_CALL && view_ptr->queue_count == 1U &&
           view_ptr->channel_list[DOMINO_BEHAVIOR_CHANNEL_MAIN].behavior_type == DOMINO_BEHAVIOR_TYPE_PHONE_CALL &&
           view_ptr->channel_list[DOMINO_BEHAVIOR_CHANNEL_AUX_1].behavior_type == DOMINO_BEHAVIOR_TYPE_MESSAGE &&
           view_ptr->channel_list[DOMINO_BEHAVIOR_CHANNEL_AUX_1].behavior_status == DOMINO_BEHAVIOR_STATUS_ACTIVE;
}

/** @brief 睡眠任务重新成为运行任务并开始新的睡眠行为。 */
static bool sleepResumed(const NpcView* view_ptr, uintptr_t sleep_task_id) {
    return view_ptr->active_task_id == (uint32_t)sleep_task_id && view_ptr->queue_count == 0U && sleepBehaviorActive(view_ptr, 0U);
}

/** @brief 复制样例存档目录，并把人物精力改写为给定值。 */
static void prepareSave(const char* source_dir, const char* target_dir, uint32_t energy) {
    const DOMINO_CODE remove_code = dominoRemovePathRecursive(target_dir);
    if (remove_code != CODE_OK || dominoMkdirRecursive(target_dir) != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(ERR_DIRECTORY_CREATE, "cannot prepare e2e save dir %s", target_dir);
    }
    DIR* dir_ptr = opendir(source_dir);
    if (!dir_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_DIRECTORY_OPEN, "cannot open sandbox dir %s", source_dir);
    }
    const struct dirent* entry_ptr;
    while ((entry_ptr = readdir(dir_ptr)) != nullptr) {
        if (entry_ptr->d_name[0] == '.') {
            continue;
        }
        char source_path[1024];
        char target_path[1024];
        snprintf(source_path, sizeof(source_path), "%s/%s", source_dir, entry_ptr->d_name);
        snprintf(target_path, sizeof(target_path), "%s/%s", target_dir, entry_ptr->d_name);
        uint8_t* bytes_ptr = nullptr;
        size_t length = 0U;
        if (dominoReadFileBytes(source_path, &bytes_ptr, &length) != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_READ, "cannot read %s", source_path);
        }
        if (strcmp(entry_ptr->d_name, "human_0.json") == 0) {
            char* found_ptr = strstr((char*)bytes_ptr, "\"energy\": 20");
            if (!found_ptr) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "sandbox human energy field not found");
            }
            char digits[3];
            snprintf(digits, sizeof(digits), "%02u", energy % 100U);
            memcpy(found_ptr + strlen("\"energy\": "), digits, 2U);
        }
        if (dominoAtomicWriteFile(target_path, bytes_ptr, length) != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_WRITE, "cannot write %s", target_path);
        }
        free(bytes_ptr);
    }
    closedir(dir_ptr);
}

/** @brief 存档目录中某个文件是否包含给定文本。 */
static bool saveFileContains(const char* save_dir, const char* file_name, const char* text_str) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", save_dir, file_name);
    uint8_t* bytes_ptr = nullptr;
    size_t length = 0U;
    if (dominoReadFileBytes(path, &bytes_ptr, &length) != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_READ, "cannot read %s", path);
    }
    bool found_flag = false;
    const size_t text_len = strlen(text_str);
    for (size_t offset = 0U; !found_flag && text_len <= length && offset <= length - text_len; offset++) {
        found_flag = memcmp(bytes_ptr + offset, text_str, text_len) == 0;
    }
    free(bytes_ptr);
    return found_flag;
}

static DominoHumanAiConfig fastSleepConfig(void) {
    DominoHumanAiConfig config = dominoHumanDefaultAiConfig();
    config.sleep_trigger_energy = 30U;
    config.sleep_target_energy = 31U;
    return config;
}

/** @brief 自主睡眠、客户端收发通道骨架、冻结、停止后读档延续、条件失效与恢复，以及仅 Init 的生命周期。 */
static void testEndToEndSleep(const char* sandbox_dir, const char* save_dir) {
    g_case_name = "e2e_sleep";
    prepareSave(sandbox_dir, save_dir, 30U);
    const DominoHumanAiConfig config = fastSleepConfig();
    dominoHumanSetNextAiConfig(&config);
    const DominoEngineLaunchConfig launch = {.storage_path = save_dir};

    /* 会话 1：Start 后依次完成动机、任务、行为规划并开始睡眠。 */
    DOMINO_PRINT_MESSAGE("e2e session 1");
    dominoEngineInit(launch);
    /* 加载后：样例人物没有动机，只置动机待评估标志；下次规划时刻不跨会话保存。 */
    CHECK(dominoHumanRuntimeOf(dominoGetHumanByID(1U))->pending_mask == DOMINO_HUMAN_PLAN_MASK_MOTIVATION &&
          dominoHumanRuntimeOf(dominoGetHumanByID(1U))->next_plan_mask == 0U);
    dominoEngineStart(nullptr);
    NpcView overview;
    CHECK(pollNpc(1U, sleepBehaviorActive, 0U, 10000U, &overview));
    DOMINO_PRINT_MESSAGE("e2e sleeping task=%u behavior=%u energy=%u", overview.active_task_id, overview.channel_list[0].behavior_id,
                         overview.energy);
    const uint32_t task_id = overview.active_task_id;
    const uint32_t behavior_id = overview.channel_list[0].behavior_id;
    CHECK(overview.found_flag && overview.motivation_count == 1U);
    CHECK(overview.active_task_type == DOMINO_TASK_TYPE_SLEEP && overview.state == DOMINO_HUMAN_STATE_NORMAL && overview.queue_count == 0U);
    CHECK(!queryNpc(99U).found_flag);
    /* 客户端收发通道骨架：尚未定义业务场景，消息可以入队，主循环按不支持的类型记录并丢弃，不回包；空指针被拒绝。 */
    DominoEngineMessage message = {.type = DOMINO_ENGINE_MSG_NONE};
    CHECK(dominoEnginePushMsg(&message) == CODE_OK && message.id != 0U);
    CHECK(dominoEnginePushMsg(nullptr) == ERR_NULL_POINTER);
    sleepMs(2000U);
    /* 冻结期间客户端消息仍可入队，主循环保留到解冻后处理；冻结时长不计入业务时间。 */
    const uint32_t before_freeze_ms = nowMs();
    const uint64_t wall_begin_ns = dominoMonotonicTimeNs();
    dominoEngineFreezeTime();
    message = (DominoEngineMessage){.type = DOMINO_ENGINE_MSG_NONE};
    CHECK(dominoEnginePushMsg(&message) == CODE_OK);
    sleepMs(800U);
    dominoEngineThawTime();
    const uint32_t after_thaw_ms = nowMs();
    const uint64_t wall_elapsed_ms = (dominoMonotonicTimeNs() - wall_begin_ns) / 1000000U;
    CHECK(after_thaw_ms >= before_freeze_ms && (uint64_t)(after_thaw_ms - before_freeze_ms) + 600U <= wall_elapsed_ms);
    sleepMs(500U);
    DominoEngineMessage reply;
    CHECK(dominoEnginePopMsg(&reply, false) == ERR_QUEUE_EMPTY);
    dominoEngineStop();
    /* Stop 结算到停钟时刻：任务仍在执行，行为进度和人物游标均已推进。 */
    const DominoHuman* human_ptr = dominoGetHumanByID(1U);
    const DominoTask* task_ptr = dominoGetTaskByID(task_id);
    const DominoBehavior* behavior_ptr = dominoGetBehaviorByID(behavior_id);
    CHECK(task_ptr != nullptr && task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && behavior_ptr != nullptr);
    CHECK(human_ptr->data->active_task_id == task_id);
    const domino_runtime_ms_t saved_progress_ms = behavior_ptr ? behavior_ptr->effective_progress_ms : 0U;
    const domino_runtime_ms_t saved_settled_ms = human_ptr->data->settled_at_ms;
    CHECK(saved_progress_ms >= 2000U && saved_settled_ms >= saved_progress_ms);
    dominoEngineDestroy();

    /* 会话 2：读档恢复原 task_id、behavior_id、进度和游标，继续执行到成功；收益与实施次数不重复。 */
    DOMINO_PRINT_MESSAGE("e2e session 2 progress_ms=%u settled_ms=%u", saved_progress_ms, saved_settled_ms);
    dominoHumanSetNextAiConfig(&config);
    dominoEngineInit(launch);
    human_ptr = dominoGetHumanByID(1U);
    behavior_ptr = dominoGetBehaviorByID(behavior_id);
    CHECK(behavior_ptr != nullptr && behavior_ptr->effective_progress_ms == saved_progress_ms && human_ptr->data->settled_at_ms == saved_settled_ms);
    CHECK(behavior_ptr != nullptr && behavior_ptr->step_seq == 1U && behavior_ptr->channel_index == DOMINO_BEHAVIOR_CHANNEL_MAIN);
    CHECK(human_ptr->data->active_task_id == task_id && dominoGetTaskByID(task_id)->current_behavior_id_list[0] == behavior_id);
    /* 读档后不沿用会话前的规划状态：有有效动机的人物动机、任务各规划一次，由规划器重新给出时刻；存档中没有动机规划重试字段。 */
    CHECK(dominoHumanRuntimeOf(human_ptr)->pending_mask == DOMINO_HUMAN_PLAN_MASK_ALL && dominoHumanRuntimeOf(human_ptr)->next_plan_mask == 0U);
    CHECK(!saveFileContains(save_dir, "human_0.json", "motivation_retry") && !saveFileContains(save_dir, "human_motivation_0.json", "retry_at_ms") &&
          !saveFileContains(save_dir, "human_motivation_0.json", "\"flags\""));
    dominoEngineStart(nullptr);
    CHECK(pollNpc(1U, successCountReached, 1U, 30000U, &overview));
    dominoEngineStop();
    const DominoMotivation* motivation_ptr = dominoHumanFindActiveMotivation(dominoGetHumanByID(1U)->data, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
    CHECK(motivation_ptr->success_count == 1U && dominoHumanMotivationHistoryAt(motivation_ptr, 0U).task_id == task_id);
    dominoEngineDestroy();

    /* 仅 Init 的生命周期：不推进游标、不结算。 */
    dominoHumanSetNextAiConfig(&config);
    dominoEngineInit(launch);
    const domino_runtime_ms_t idle_settled_ms = dominoGetHumanByID(1U)->data->settled_at_ms;
    dominoEngineStop();
    CHECK(dominoGetHumanByID(1U)->data->settled_at_ms == idle_settled_ms);
    dominoEngineDestroy();

    /* 会话 3：新需求开启第二轮；睡眠条件失效时取消主行为，恢复后由行为规划器修复，替代行为取得新 ID。 */
    DOMINO_PRINT_MESSAGE("e2e session 3");
    dominoHumanSetNextAiConfig(&config);
    dominoEngineInit(launch);
    dominoEngineStart(nullptr);
    /* 测试验收专用的人物输入：越界精力和不合法的外部信息类别在入口被拒绝，不投递。 */
    CHECK(dominoHumanSubmitEnergyForTest(1U, DOMINO_HUMAN_NEED_MAX + 1U) == ERR_INVALID_PARAM);
    CHECK(dominoHumanSubmitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_COUNT, 1U) == ERR_INVALID_PARAM);
    CHECK(dominoHumanSubmitEnergyForTest(1U, 29U) == CODE_OK);
    CHECK(pollNpc(1U, sleepBehaviorActive, 0U, 10000U, &overview));
    const uint32_t round2_task_id = overview.active_task_id;
    const uint32_t round2_behavior_id = overview.channel_list[0].behavior_id;
    CHECK(round2_task_id > task_id && round2_behavior_id > behavior_id);
    dominoHumanSubmitSleepConditionForTest(1U, true);
    CHECK(pollNpc(1U, sleepBehaviorStopped, 0U, 5000U, &overview));
    CHECK(overview.active_task_id == round2_task_id && overview.active_task_exec_status == DOMINO_TASK_EXEC_STATUS_OPEN);
    dominoHumanSubmitSleepConditionForTest(1U, false);
    CHECK(pollNpc(1U, sleepBehaviorActive, round2_behavior_id, 10000U, &overview));
    CHECK(overview.active_task_id == round2_task_id && overview.channel_list[0].behavior_id > round2_behavior_id);
    CHECK(pollNpc(1U, successCountReached, 2U, 40000U, &overview));
    dominoEngineStop();
    const DominoHumanExecutorStats executor_stats = dominoHumanExecutionGetStats();
    CHECK(executor_stats.accepted_candidate_count >= 3U && executor_stats.batch_count > 0U);
    for (uint8_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        const DominoHumanPlannerStats planner_stats = dominoHumanPlannerGetStats(kind);
        DOMINO_PRINT_MESSAGE("planner kind=%u batches=%" PRIu64 " high=%" PRIu64 " normal=%" PRIu64 " dropped=%" PRIu64 " max_request_ns=%" PRIu64,
                             kind, planner_stats.batch_count, planner_stats.request_count_list[0], planner_stats.request_count_list[1],
                             planner_stats.dropped_count, planner_stats.max_request_ns);
    }
    DOMINO_PRINT_MESSAGE("executor batches=%" PRIu64 " accepted=%" PRIu64 " rejected=%" PRIu64 " stale=%" PRIu64 " max_result_wait_ns=%" PRIu64
                         " write_lock_ns=%" PRIu64,
                         executor_stats.batch_count, executor_stats.accepted_candidate_count, executor_stats.rejected_candidate_count,
                         executor_stats.stale_result_count, executor_stats.max_result_wait_ns, executor_stats.write_lock_ns);
    dominoEngineDestroy();
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    CHECK(g_view_failure_count == 0U);
    dominoHumanSetNextAiConfig(nullptr);
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 端到端应激：真实线程下睡眠遇到联系信息完全让位，回应通话与伴随的消息记录并行；停止保存后读档继续，通话结束后睡眠恢复。   */
/* ---------------------------------------------------------------------------------------------------------------- */

static void setContactPlanners(bool enable_flag) {
    dominoHumanPlannerSetComputeOverride(DOMINO_HUMAN_PLANNER_MOTIVATION, enable_flag ? contactMotivationCompute : nullptr);
    dominoHumanPlannerSetComputeOverride(DOMINO_HUMAN_PLANNER_TASK, enable_flag ? contactTaskCompute : nullptr);
    dominoHumanPlannerSetComputeOverride(DOMINO_HUMAN_PLANNER_BEHAVIOR, enable_flag ? contactBehaviorCompute : nullptr);
}

static void testEndToEndStimulus(const char* sandbox_dir, const char* save_dir) {
    g_case_name = "e2e_stimulus";
    prepareSave(sandbox_dir, save_dir, 20U);
    DominoHumanAiConfig config = dominoHumanDefaultAiConfig();
    config.sleep_target_energy = 40U;
    dominoHumanSetNextAiConfig(&config);
    g_contact_source_offset = 0U;
    g_contact_call_ms = 6000U;
    setContactPlanners(true);
    const DominoEngineLaunchConfig launch = {.storage_path = save_dir};
    dominoEngineInit(launch);
    dominoEngineStart(nullptr);
    NpcView overview;
    CHECK(pollNpc(1U, sleepBehaviorActive, 0U, 15000U, &overview));
    const uint32_t sleep_task_id = overview.active_task_id;
    const uint32_t first_sleep_behavior_id = overview.channel_list[DOMINO_BEHAVIOR_CHANNEL_MAIN].behavior_id;
    CHECK(dominoHumanSubmitStimulusForTest(1U, DOMINO_HUMAN_STIMULUS_CONTACT, 42U) == CODE_OK);
    CHECK(pollNpc(1U, respondRunning, 0U, 15000U, &overview));
    const uint32_t phone_task_id = overview.active_task_id;
    CHECK(phone_task_id != sleep_task_id && overview.motivation_count == 2U);
    dominoEngineStop();
    /* Stop 结算到停钟时刻：睡眠让位排在队首，回应任务运行中，伴随依赖仍待来源结束。 */
    const DominoHumanData* data_ptr = dominoGetHumanByID(1U)->data;
    const DominoTask* phone_ptr = dominoGetTaskByID(phone_task_id);
    const DominoTask* sleep_ptr = dominoGetTaskByID(sleep_task_id);
    CHECK(data_ptr->active_task_id == phone_task_id && data_ptr->task_queue_count == 1U && data_ptr->task_queue_list[0] == sleep_task_id);
    CHECK(data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE && data_ptr->stimulus_round == 1U);
    CHECK(sleep_ptr != nullptr && sleep_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN && sleep_ptr->main_cancelled_count == 1U);
    CHECK(phone_ptr != nullptr && phone_ptr->work_source_kind == DOMINO_TASK_SOURCE_STIMULUS && phone_ptr->current_behavior_id_list[1] != 0U);
    const domino_behavior_id_t record_behavior_id = phone_ptr->current_behavior_id_list[1];
    const uint32_t call_step_seq = dominoGetBehaviorByID(phone_ptr->current_behavior_id_list[0])->step_seq;
    const domino_runtime_ms_t sleep_started_ms = sleep_ptr->started_at_ms;
    dominoEngineDestroy();

    /* 读档：运行任务、待执行队列、伴随依赖和外部信息轮次原样恢复；启动后通话结束，伴随记录随之取消，睡眠恢复。 */
    dominoHumanSetNextAiConfig(&config);
    dominoEngineInit(launch);
    data_ptr = dominoGetHumanByID(1U)->data;
    const DominoBehavior* record_ptr = dominoGetBehaviorByID(record_behavior_id);
    CHECK(data_ptr->active_task_id == phone_task_id && data_ptr->task_queue_count == 1U && data_ptr->task_queue_list[0] == sleep_task_id);
    CHECK(data_ptr->stimulus_round == 1U && data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE);
    CHECK(record_ptr != nullptr && record_ptr->dependency_list[0].kind == DOMINO_BEHAVIOR_DEPENDENCY_WHILE &&
          record_ptr->dependency_list[0].step_seq == call_step_seq && record_ptr->dependency_list[0].state == DOMINO_BEHAVIOR_DEPENDENCY_PENDING);
    dominoEngineStart(nullptr);
    CHECK(pollNpc(1U, sleepResumed, sleep_task_id, 15000U, &overview));
    CHECK(overview.channel_list[DOMINO_BEHAVIOR_CHANNEL_MAIN].behavior_id > first_sleep_behavior_id);
    dominoEngineStop();
    const DominoMotivation* contact_ptr = dominoHumanFindActiveMotivation(dominoGetHumanByID(1U)->data, DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT);
    CHECK(contact_ptr != nullptr && contact_ptr->success_count == 1U && dominoHumanMotivationHistoryAt(contact_ptr, 0U).task_id == phone_task_id);
    CHECK(dominoGetTaskByID(phone_task_id) == nullptr && dominoGetTaskByID(sleep_task_id)->started_at_ms == sleep_started_ms);
    dominoEngineDestroy();
    setContactPlanners(false);
    g_contact_call_ms = 3000U;
    dominoHumanSetNextAiConfig(nullptr);
    CHECK(dominoHumanCandidateBytesInUse() == 0U);
    CHECK(g_view_failure_count == 0U);
}

int dominoHumanPlayground(const char* sandbox_dir, const char* work_dir) {
    testAutonomousSleep();
    testSettlementEquivalence();
    testPredictArithmetic();
    testNaturalMinuteAlignment();
    testExternalSatisfaction();
    testSleepConditionRepair();
    testMotivationHistory();
    testRequestIdentity();
    testCandidateCommit();
    testInformationGap();
    testBehaviorDependencies();
    testBehaviorCandidateDependencies();
    testDeadlineBoundary();
    testTerminalDetailRetention();
    testSameBoundaryInterplay();
    testLateNotification();
    testSharedDecision();
    testStimulusInsert();
    testStimulusYield();
    testStimulusRounds();
    testTestInputs();
    testMotivationTriggers();
    testMotivationTermination();
    testTaskTriggers();
    testNextPlanTimes();
    testBehaviorReview();
    testBehaviorScope();
    DOMINO_PRINT_MESSAGE("npc_ai fixtures: %d checks, %d failures", g_check_count, g_failure_count);
    char save_dir[1024];
    snprintf(save_dir, sizeof(save_dir), "%s/npc_ai_e2e", work_dir);
    testEndToEndSleep(sandbox_dir, save_dir);
    snprintf(save_dir, sizeof(save_dir), "%s/npc_ai_stimulus", work_dir);
    testEndToEndStimulus(sandbox_dir, save_dir);
    DOMINO_PRINT_MESSAGE("npc_ai total: %d checks, %d failures", g_check_count, g_failure_count);
    return g_failure_count;
}
