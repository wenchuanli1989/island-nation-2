#include "advance.h"
#include "candidate.h"
#include "entry.h"
#include "motivation/entry.h"
#include "planner.h"
#include "rules.h"

/** @file policy.c
 * @brief 第一阶段三类规划器的最小睡眠策略；在规划线程持有的 human 读锁内只读计算，只产生本层候选。
 * @note 需要输入时刻的指标时，从人物共同游标出发做只读预测，不补写结算或前移游标；计算量计入单请求预算，耗尽返回暂时无方案。
 * 动机和任务规划随每种结果给出下次规划时刻或“无”；暂时无方案时给出规划时刻 + retry_delay_ms，超出 32 位业务毫秒时为“无”。
 */

/** @brief 预测规划时刻的需求值；超出预算或预测步数返回 false。 */
static bool predictNeedsNow(const DominoHuman* human_ptr, domino_runtime_ms_t now_ms, uint32_t compute_budget, DominoHumanNeedValues* values_ptr,
                            uint32_t* work_count_ptr) {
    return dominoHumanPredictNeedsAt(human_ptr, now_ms, values_ptr, work_count_ptr) && *work_count_ptr <= compute_budget;
}

/** @brief 给出 at_ms 作为下次规划时刻；超出 32 位业务毫秒时视为“无”。 */
static void setNextPlanAt(DominoHumanPlanOutput* output_ptr, uint64_t at_ms) {
    if (at_ms <= UINT32_MAX) {
        output_ptr->next_plan_at_ms = (domino_runtime_ms_t)at_ms;
        output_ptr->next_plan_valid_flag = true;
    }
}

/** @brief 暂时无方案：规划时刻 + retry_delay_ms 再看一次。 */
static uint8_t noPlanRetryLater(DominoHumanPlanOutput* output_ptr, domino_runtime_ms_t now_ms) {
    setNextPlanAt(output_ptr, (uint64_t)now_ms + g_domino_human_ai_config.retry_delay_ms);
    return DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN;
}

uint8_t dominoHumanPlanMotivations(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                   uint32_t compute_budget, DominoHumanPlanOutput* output_ptr) {
    (void)slot_ptr;
    (void)compute_budget;
    if (dominoHumanFindActiveMotivation(human_ptr->data, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY) != nullptr) {
        /* 恢复精力是维持型动机，不终结；已有时无需调整，也不需要以后再看。 */
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE;
    }
    void* candidate_ptr = dominoHumanCandidateAlloc(DOMINO_HUMAN_PLANNER_MOTIVATION, 1U, 1U, 0U);
    if (candidate_ptr == nullptr) {
        return noPlanRetryLater(output_ptr, now_ms);
    }
    /* 最小策略每人物只保留一项有效的恢复精力动机；新建后由任务规划器决定何时为它安排睡眠。 */
    *(DominoMotivationCandidateEntry*)dominoHumanCandidateEntries(candidate_ptr) = (DominoMotivationCandidateEntry){
        .op = DOMINO_MOTIVATION_CANDIDATE_CREATE,
        .type = DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY,
    };
    output_ptr->candidate_ptr = candidate_ptr;
    return DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE;
}

uint8_t dominoHumanPlanTasks(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms, uint32_t compute_budget,
                             DominoHumanPlanOutput* output_ptr) {
    (void)slot_ptr;
    /* 第一阶段生产策略只为恢复精力安排睡眠，不产生回应外部信息的任务。 */
    const DominoMotivation* motivation_ptr = dominoHumanFindActiveMotivation(human_ptr->data, DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY);
    if (motivation_ptr == nullptr || motivation_ptr->round_task_id != 0U || motivation_ptr->need_round == UINT32_MAX) {
        /* 没有恢复精力动机、本轮睡眠工作尚未结束或轮次已耗尽：无需调整。动机集合变化和任务结束都会置任务待规划标志。 */
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE;
    }
    const uint8_t trigger = g_domino_human_ai_config.sleep_trigger_energy;
    const uint32_t step_max = compute_budget < DOMINO_HUMAN_PREDICT_STEP_MAX ? compute_budget : DOMINO_HUMAN_PREDICT_STEP_MAX;
    uint64_t drop_ms;
    uint32_t work_count = 0U;
    if (!dominoHumanPredictEnergyDropAt(human_ptr, now_ms, trigger, step_max, &drop_ms, &work_count)) {
        return noPlanRetryLater(output_ptr, now_ms);
    }
    if (drop_ms > now_ms) {
        /* 精力高于触发值：在预测精力首次降到触发值的时刻再看一次。 */
        setNextPlanAt(output_ptr, drop_ms);
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE;
    }
    void* candidate_ptr = dominoHumanCandidateAlloc(DOMINO_HUMAN_PLANNER_TASK, 1U, 1U, 0U);
    if (candidate_ptr == nullptr) {
        return noPlanRetryLater(output_ptr, now_ms);
    }
    /* 没有独立业务来源的睡眠使用动机身份及下一需求轮次作为工作身份；提出后不需要以后再看，任务结束时会置标志。 */
    *(DominoTaskCandidateEntry*)dominoHumanCandidateEntries(candidate_ptr) = (DominoTaskCandidateEntry){
        .motivation_id = motivation_ptr->motivation_id,
        .work_source_id = motivation_ptr->motivation_id,
        .work_round = motivation_ptr->need_round + 1U,
        .expected_motivation_round = motivation_ptr->need_round,
        .op = DOMINO_TASK_CANDIDATE_CREATE,
        .type = DOMINO_TASK_TYPE_SLEEP,
        .insert_index = DOMINO_PLAN_CANDIDATE_INSERT_APPEND,
        .target_value = g_domino_human_ai_config.sleep_target_energy,
        .work_source_kind = DOMINO_TASK_SOURCE_MOTIVATION,
    };
    output_ptr->candidate_ptr = candidate_ptr;
    return DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE;
}

uint8_t dominoHumanPlanBehaviors(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                 uint32_t compute_budget, DominoHumanPlanOutput* output_ptr) {
    const DominoTask* task_ptr = slot_ptr->scope_kind == DOMINO_HUMAN_PLAN_SCOPE_TASK ? dominoGetTaskById(slot_ptr->scope_id) : nullptr;
    if (task_ptr == nullptr || task_ptr->owner_human_id != human_ptr->human_id || !dominoHumanTaskNeedsBehaviorPlan(human_ptr, task_ptr)) {
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE;
    }
    if (task_ptr->type != DOMINO_TASK_TYPE_SLEEP) {
        /* 第一阶段生产策略只为睡眠安排行为，不安排其他类型及插入的辅助行为。 */
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN;
    }
    DominoHumanNeedValues needs;
    uint32_t work_count = 0U;
    if (!predictNeedsNow(human_ptr, now_ms, compute_budget, &needs, &work_count)) {
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN;
    }
    if (needs.energy >= task_ptr->target_value) {
        /* 指标已满足时由任务决策撤销或取消，不安排凑数的睡眠。 */
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE;
    }
    /* 多恢复 1 点吸收启动时刻与规划时刻之间可能多出的一次自然衰减。 */
    const uint8_t goal = task_ptr->target_value < 100U ? (uint8_t)(task_ptr->target_value + 1U) : task_ptr->target_value;
    domino_runtime_ms_t duration_ms;
    if (!dominoHumanCalcSleepDuration(needs.energy, now_ms, goal, compute_budget - work_count, &duration_ms, &work_count)) {
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN;
    }
    void* candidate_ptr = dominoHumanCandidateAlloc(DOMINO_HUMAN_PLANNER_BEHAVIOR, 1U, 1U, 1U);
    if (candidate_ptr == nullptr) {
        return DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN;
    }
    DominoBehaviorCandidateEntry entry = {
        .expected_plan_version = task_ptr->plan_version,
        .task_id = task_ptr->task_id,
        .step_index = 0U,
        .step_count = 1U,
        .op = DOMINO_BEHAVIOR_CANDIDATE_REPLACE,
        .depends_current_flag = 1U,
    };
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        entry.expected_current_behavior_id_list[channel] = task_ptr->current_behavior_id_list[channel];
    }
    *(DominoBehaviorCandidateEntry*)dominoHumanCandidateEntries(candidate_ptr) = entry;
    dominoHumanCandidateSteps(candidate_ptr)[0] = (DominoBehaviorPlanItem){
        .target_effective_ms = duration_ms, .type = DOMINO_BEHAVIOR_TYPE_SLEEP, .channel_index = DOMINO_BEHAVIOR_CHANNEL_MAIN};
    output_ptr->candidate_ptr = candidate_ptr;
    return DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE;
}
