#include "sleep.h"

static DominoTaskDecision decideSleepTask(const DominoTaskDecisionInput* input_ptr) {
    const DominoTask* task_ptr = input_ptr->task_ptr;
    const DominoBehavior* main_ptr = input_ptr->behavior_list[DOMINO_BEHAVIOR_CHANNEL_MAIN];
    const bool target_reached = input_ptr->energy >= task_ptr->target_value;
    const bool externally_satisfied = (task_ptr->flags & DOMINO_TASK_FLAG_NEED_SATISFIED_EXTERNALLY) != 0U;
    if (task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_NONE) {
        /* 未启动工作没有成功行为；需求已被满足时撤销计划并结束本轮。 */
        if (externally_satisfied || target_reached) {
            return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_REVOKE, .reason = DOMINO_END_REASON_NEED_SATISFIED};
        }
        return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_CONTINUE};
    }
    if (task_ptr->main_succeeded_count > 0U && target_reached) {
        return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_SUCCEED, .reason = DOMINO_END_REASON_COMPLETED};
    }
    if (main_ptr != nullptr) {
        if (input_ptr->condition.sleep_forbidden_flag) {
            return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_REPAIR,
                                        .behavior_end_status = DOMINO_BEHAVIOR_STATUS_CANCELLED,
                                        .channel_mask = 1U << DOMINO_BEHAVIOR_CHANNEL_MAIN,
                                        .reason = DOMINO_END_REASON_CONDITION_LOST};
        }
        if (externally_satisfied && task_ptr->main_succeeded_count == 0U) {
            return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_CANCEL, .reason = DOMINO_END_REASON_NEED_SATISFIED};
        }
        /* 自身睡眠恢复的精力可能早于行为目标时长达到指标；主行为结束后再判定任务成功。 */
        return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_CONTINUE};
    }
    if (target_reached && task_ptr->main_succeeded_count == 0U) {
        return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_CANCEL, .reason = DOMINO_END_REASON_NEED_SATISFIED};
    }
    return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_CONTINUE};
}

/** @brief 主通道既没有当前行为也没有自身步骤，或有依赖失效的步骤，就缺少安排。 */
static bool hasSleepPlanGap(const DominoTaskDecisionInput* input_ptr) {
    return dominoHumanTaskHasBrokenStep(input_ptr->task_ptr) || !dominoHumanTaskHasOwnWork(input_ptr, 1U << DOMINO_BEHAVIOR_CHANNEL_MAIN);
}

static bool canPlanSleepBehavior(const DominoTaskDecisionInput* input_ptr) {
    return !input_ptr->condition.sleep_forbidden_flag;
}

/** @brief 睡眠中不能同时回应联系：完全让位，回应完成后再修复剩余睡眠。 */
static uint8_t reactSleep(const DominoTaskDecisionInput* input_ptr, uint8_t stimulus_kind) {
    (void)input_ptr;
    (void)stimulus_kind;
    return DOMINO_TASK_REACTION_YIELD;
}

static void onSleepEnergyInput(DominoTask* task_ptr, uint8_t energy) {
    if (dominoTaskIsLive(task_ptr) && task_ptr->main_succeeded_count == 0U && energy >= task_ptr->target_value) {
        task_ptr->flags |= DOMINO_TASK_FLAG_NEED_SATISFIED_EXTERNALLY;
    }
}

const DominoTaskRules domino_sleep_task_rules = {
    .decide = decideSleepTask,
    .has_plan_gap = hasSleepPlanGap,
    .can_plan_behavior = canPlanSleepBehavior,
    .react = reactSleep,
    .on_energy_input = onSleepEnergyInput,
};
