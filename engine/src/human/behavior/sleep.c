#include "sleep.h"

#include "../rules.h"

static bool validateSleepStep(const DominoBehaviorPlanItem* item_ptr) {
    return item_ptr->target_effective_ms > 0U;
}

static bool canRunSleep(const DominoHumanConditionView* view_ptr) {
    return !view_ptr->sleep_forbidden_flag;
}

/** @brief 有效进度每跨过一个 DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS 整周期恢复 1 点精力。 */
static void calcSleep(const DominoBehaviorCalcInput* input_ptr, DominoBehaviorCalcOutput* output_ptr) {
    *output_ptr = (DominoBehaviorCalcOutput){.progress_ms = input_ptr->progress_ms};
    if (input_ptr->status != DOMINO_BEHAVIOR_STATUS_ACTIVE || !input_ptr->condition_ok || input_ptr->progress_ms >= input_ptr->target_ms) {
        return;
    }
    const domino_runtime_ms_t remaining_ms = input_ptr->target_ms - input_ptr->progress_ms;
    const domino_runtime_ms_t effective_ms = input_ptr->span_ms < remaining_ms ? input_ptr->span_ms : remaining_ms;
    output_ptr->progress_ms = input_ptr->progress_ms + effective_ms;
    const uint32_t point_count = dominoBehaviorCountPeriods(input_ptr->progress_ms, output_ptr->progress_ms, DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS);
    output_ptr->energy_delta = (int32_t)(point_count > 100U ? 100U : point_count);
    const domino_runtime_ms_t left_ms = remaining_ms - effective_ms;
    const domino_runtime_ms_t point_ms = dominoBehaviorMsUntilPeriod(output_ptr->progress_ms, DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS);
    output_ptr->next_boundary_ms = point_ms < left_ms ? point_ms : left_ms;
    output_ptr->has_boundary = left_ms > 0U;
}

static uint8_t judgeSleep(const DominoBehavior* behavior_ptr) {
    return behavior_ptr->effective_progress_ms >= behavior_ptr->target_effective_ms ? DOMINO_BEHAVIOR_STATUS_SUCCEEDED
                                                                                    : DOMINO_BEHAVIOR_STATUS_ACTIVE;
}

const DominoBehaviorRules g_domino_sleep_behavior_rules = {
    .validate_step = validateSleepStep,
    .can_run = canRunSleep,
    .calc = calcSleep,
    .judge = judgeSleep,
};
