#include "entry.h"

#include "../entry.h"
#include "sleep.h"

bool dominoHumanTaskIsWithinDeadline(const DominoTask* task_ptr, domino_runtime_ms_t now_ms) {
    if ((task_ptr->flags & DOMINO_TASK_FLAG_DEADLINE_VALID) == 0U) {
        return true;
    }
    return (task_ptr->flags & DOMINO_TASK_FLAG_DEADLINE_INCLUSIVE) != 0U ? now_ms <= task_ptr->deadline_ms : now_ms < task_ptr->deadline_ms;
}

bool dominoHumanTaskHasOwnWork(const DominoTaskDecisionInput* input_ptr, uint8_t channel_mask) {
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const DominoBehavior* behavior_ptr = input_ptr->behavior_list[channel];
        if ((channel_mask & (1U << channel)) != 0U && behavior_ptr != nullptr && (behavior_ptr->flags & DOMINO_BEHAVIOR_FLAG_INSERTED) == 0U) {
            return true;
        }
    }
    const DominoTask* task_ptr = input_ptr->task_ptr;
    for (uint8_t index = 0U; index < task_ptr->plan_item_count; index++) {
        const DominoBehaviorPlanItem* item_ptr = &task_ptr->plan_item_list[index];
        if ((channel_mask & (1U << item_ptr->channel_index)) != 0U && (item_ptr->flags & DOMINO_BEHAVIOR_FLAG_INSERTED) == 0U) {
            return true;
        }
    }
    return false;
}

bool dominoHumanTaskHasInsertedWork(const DominoTaskDecisionInput* input_ptr) {
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const DominoBehavior* behavior_ptr = input_ptr->behavior_list[channel];
        if (behavior_ptr != nullptr && (behavior_ptr->flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U) {
            return true;
        }
    }
    const DominoTask* task_ptr = input_ptr->task_ptr;
    for (uint8_t index = 0U; index < task_ptr->plan_item_count; index++) {
        if ((task_ptr->plan_item_list[index].flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U) {
            return true;
        }
    }
    return false;
}

bool dominoHumanTaskHasBrokenStep(const DominoTask* task_ptr) {
    for (uint8_t index = 0U; index < task_ptr->plan_item_count; index++) {
        const DominoBehaviorPlanItem* item_ptr = &task_ptr->plan_item_list[index];
        for (uint8_t dependency_index = 0U; dependency_index < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; dependency_index++) {
            if (item_ptr->dependency_list[dependency_index].kind != DOMINO_BEHAVIOR_DEPENDENCY_NONE &&
                item_ptr->dependency_list[dependency_index].state == DOMINO_BEHAVIOR_DEPENDENCY_BROKEN) {
                return true;
            }
        }
    }
    return false;
}

/** @brief 通用计时任务：至少一个主行为成功且自身工作全部完成即成功；到截止点仍未完成则失败（未启动的撤销）。
 * @note 依赖失效的步骤作为计划缺口交给行为规划修复。
 */
static DominoTaskDecision decideTimedTask(const DominoTaskDecisionInput* input_ptr) {
    const DominoTask* task_ptr = input_ptr->task_ptr;
    const bool deadline_reached = (task_ptr->flags & DOMINO_TASK_FLAG_DEADLINE_VALID) != 0U && input_ptr->now_ms >= task_ptr->deadline_ms;
    if (task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_NONE) {
        if (deadline_reached) {
            // 到截止点仍未启动，已不可能按时完成。
            return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_REVOKE, .reason = DOMINO_END_REASON_DEADLINE_MISSED};
        }
        return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_CONTINUE};
    }
    const bool ready = task_ptr->main_succeeded_count > 0U && !dominoHumanTaskHasOwnWork(input_ptr, DOMINO_BEHAVIOR_CHANNEL_MASK_ALL);
    if (ready && dominoHumanTaskIsWithinDeadline(task_ptr, input_ptr->now_ms)) {
        return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_SUCCEED, .reason = DOMINO_END_REASON_COMPLETED};
    }
    if (deadline_reached) {
        // 本边界的行为结果已先判定，仍未完成就不能再等。
        return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_FAIL, .reason = DOMINO_END_REASON_DEADLINE_MISSED};
    }
    return (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_CONTINUE};
}

/** @brief 有依赖失效的步骤，或尚无主行为成功而自身工作已耗尽时，缺少安排。 */
static bool hasTimedPlanGap(const DominoTaskDecisionInput* input_ptr) {
    const DominoTask* task_ptr = input_ptr->task_ptr;
    return dominoHumanTaskHasBrokenStep(task_ptr) ||
           (task_ptr->main_succeeded_count == 0U && !dominoHumanTaskHasOwnWork(input_ptr, DOMINO_BEHAVIOR_CHANNEL_MASK_ALL));
}

static bool canPlanTimedBehavior(const DominoTaskDecisionInput* input_ptr) {
    (void)input_ptr;
    return true;
}

/** @brief 通用计时工作可以边做边回应联系：接受插入。 */
static uint8_t reactTimed(const DominoTaskDecisionInput* input_ptr, uint8_t stimulus_kind) {
    (void)input_ptr;
    (void)stimulus_kind;
    return DOMINO_TASK_REACTION_INSERT;
}

static void onTimedEnergyInput(DominoTask* task_ptr, uint8_t energy) {
    (void)task_ptr;
    (void)energy;
}

static const DominoTaskRules g_timed_task_rules = {
    .decide = decideTimedTask,
    .has_plan_gap = hasTimedPlanGap,
    .can_plan_behavior = canPlanTimedBehavior,
    .react = reactTimed,
    .on_energy_input = onTimedEnergyInput,
};

static const DominoTaskRules* const g_task_rules_by_type[DOMINO_TASK_TYPE_COUNT] = {
    [DOMINO_TASK_TYPE_SLEEP] = &g_domino_sleep_task_rules,
    [DOMINO_TASK_TYPE_PHONE_CALL] = &g_timed_task_rules,
    [DOMINO_TASK_TYPE_MESSAGE] = &g_timed_task_rules,
};

const DominoTaskRules* dominoHumanGetTaskRules(uint8_t type) {
    if (type == DOMINO_TASK_TYPE_NONE || type >= DOMINO_TASK_TYPE_COUNT) {
        return nullptr;
    }
    return g_task_rules_by_type[type];
}
