#include "entry.h"

#include "sleep.h"

/** @brief 通用计时行为：ACTIVE 时按有效时长累计进度，param 为精力消耗周期（0 为不消耗），有效进度每跨过一个整周期消耗 1 点，达到目标即成功。 */
static bool validateTimedStep(const DominoBehaviorPlanItem* item_ptr) {
    return item_ptr->target_effective_ms > 0U;
}

static bool canRunTimed(const DominoHumanConditionView* view_ptr) {
    (void)view_ptr;
    return true;
}

static void calcTimed(const DominoBehaviorCalcInput* input_ptr, DominoBehaviorCalcOutput* output_ptr) {
    *output_ptr = (DominoBehaviorCalcOutput){.progress_ms = input_ptr->progress_ms};
    if (input_ptr->status != DOMINO_BEHAVIOR_STATUS_ACTIVE || !input_ptr->condition_ok || input_ptr->progress_ms >= input_ptr->target_ms) {
        return;
    }
    const domino_runtime_ms_t remaining_ms = input_ptr->target_ms - input_ptr->progress_ms;
    const domino_runtime_ms_t effective_ms = input_ptr->span_ms < remaining_ms ? input_ptr->span_ms : remaining_ms;
    output_ptr->progress_ms = input_ptr->progress_ms + effective_ms;
    domino_runtime_ms_t next_ms = remaining_ms - effective_ms;
    if (input_ptr->param != 0U) {
        const uint32_t cost_count = dominoBehaviorCountPeriods(input_ptr->progress_ms, output_ptr->progress_ms, input_ptr->param);
        output_ptr->energy_delta = -(int32_t)(cost_count > 100U ? 100U : cost_count);
        const domino_runtime_ms_t cost_ms = dominoBehaviorCalcPeriodGapMs(output_ptr->progress_ms, input_ptr->param);
        if (cost_ms < next_ms) {
            next_ms = cost_ms;
        }
    }
    output_ptr->next_boundary_ms = next_ms;
    output_ptr->has_boundary = next_ms > 0U;
}

static uint8_t judgeTimed(const DominoBehavior* behavior_ptr) {
    return behavior_ptr->effective_progress_ms >= behavior_ptr->target_effective_ms ? DOMINO_BEHAVIOR_STATUS_SUCCEEDED
                                                                                    : DOMINO_BEHAVIOR_STATUS_ACTIVE;
}

static const DominoBehaviorRules g_timed_behavior_rules = {
    .validate_step = validateTimedStep,
    .can_run = canRunTimed,
    .calc = calcTimed,
    .judge = judgeTimed,
};

static const DominoBehaviorRules* const g_behavior_rules[DOMINO_BEHAVIOR_TYPE_COUNT] = {
    [DOMINO_BEHAVIOR_TYPE_SLEEP] = &domino_sleep_behavior_rules,
    [DOMINO_BEHAVIOR_TYPE_WORK] = &g_timed_behavior_rules,
    [DOMINO_BEHAVIOR_TYPE_PHONE_CALL] = &g_timed_behavior_rules,
    [DOMINO_BEHAVIOR_TYPE_MESSAGE] = &g_timed_behavior_rules,
};

const DominoBehaviorRules* dominoHumanGetBehaviorRules(uint8_t type) {
    if (type == DOMINO_BEHAVIOR_TYPE_NONE || type >= DOMINO_BEHAVIOR_TYPE_COUNT) {
        return nullptr;
    }
    return g_behavior_rules[type];
}

DominoBehaviorCalcOutput dominoHumanCalcBehavior(const DominoBehavior* behavior_ptr, const DominoHumanConditionView* view_ptr,
                                                 domino_runtime_ms_t span_ms) {
    const DominoBehaviorRules* rules_ptr = dominoHumanGetBehaviorRules(behavior_ptr->type);
    const DominoBehaviorCalcInput input = {
        .progress_ms = behavior_ptr->effective_progress_ms,
        .target_ms = behavior_ptr->target_effective_ms,
        .span_ms = span_ms,
        .param = behavior_ptr->param,
        .status = behavior_ptr->status,
        .condition_ok = rules_ptr->can_run(view_ptr),
    };
    DominoBehaviorCalcOutput output;
    rules_ptr->calc(&input, &output);
    return output;
}
