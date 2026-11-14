#include "advance.h"
#include "behavior/entry.h"
#include "candidate.h"
#include "motivation/entry.h"
#include "needs.h"
#include "rules.h"
#include "runtime.h"
#include "task/entry.h"

DominoPlanCandidate* dominoHumanPlanMotivations(const DominoHuman* human, DominoPlanScope scope, uint32_t now, uint32_t budget,
                                                DominoPlanOutcome* outcome) {
    *outcome = DOMINO_PLAN_UNAVAILABLE;
    if (!budget)
        return nullptr;
    const DominoMotivation* existing = dominoGetMotivationByID(scope.object_id);
    if (scope.object_id && (!existing || existing->status != DOMINO_MOTIVATION_VALID)) {
        *outcome = DOMINO_PLAN_UNCHANGED;
        return nullptr;
    }
    DominoNeedValues needs;
    if (!dominoHumanPredictNeeds(human, now, &budget, &needs))
        return nullptr;
    DominoMotivationCandidate* candidate = dominoHumanAllocateCandidate(sizeof(*candidate) + sizeof(candidate->items[0]), 1, 0);
    candidate->items[0] = (DominoMotivationSuggestion){.motivation_id = scope.object_id,
                                                       .type = DOMINO_MOTIVATION_RECOVER_ENERGY,
                                                       .needs_action = needs.energy <= DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY};
    *outcome = DOMINO_PLAN_CANDIDATE;
    return &candidate->header;
}
DominoPlanCandidate* dominoHumanPlanTasks(const DominoHuman* human, DominoPlanScope scope, uint32_t now, uint32_t budget,
                                          DominoPlanOutcome* outcome) {
    *outcome = DOMINO_PLAN_UNAVAILABLE;
    if (!budget)
        return nullptr;
    DominoNeedValues needs;
    if (!dominoHumanPredictNeeds(human, now, &budget, &needs))
        return nullptr;
    if (scope.accepted_task) {
        const DominoTask* task = dominoGetTaskByID(scope.object_id);
        if (!task || !dominoTaskQuery(task, human->data, now).needs_arrangement) {
            *outcome = DOMINO_PLAN_UNCHANGED;
            return nullptr;
        }
        if (task->type != DOMINO_TASK_TYPE_SLEEP)
            return nullptr;
        DominoTaskCandidate* candidate = dominoHumanAllocateCandidate(sizeof(*candidate) + sizeof(candidate->items[0]), 1, 0);
        DominoTaskSuggestion* item = &candidate->items[0];
        *item = (DominoTaskSuggestion){.expected_arrangement_version = task->arrangement_version,
                                       .expected_queue_version = human->runtime->channel_version[task->channel_index],
                                       .work_round = task->work_round,
                                       .not_before = task->not_before,
                                       .finish_deadline = task->finish_deadline,
                                       .task_id = task->task_id,
                                       .motivation_id = task->motivation_id,
                                       .type = task->type,
                                       .channel_index = task->channel_index,
                                       .target_energy = task->target_energy,
                                       .dependency_count = task->dependency_count,
                                       .finish_inclusive = task->finish_inclusive};
        for (uint8_t d = 0; d < task->dependency_count; ++d) {
            item->dependencies[d] = task->dependencies[d];
            item->dependency_local[d] = DOMINO_PLAN_LOCAL_NONE;
        }
        for (uint8_t q = 0; q < human->data->queue_count[task->channel_index]; ++q)
            if (human->data->channel_queue[task->channel_index][q] == task->task_id)
                item->queue_position = q;
        *outcome = DOMINO_PLAN_CANDIDATE;
        return &candidate->header;
    }
    const DominoMotivation* motivation = dominoGetMotivationByID(scope.object_id);
    if (!motivation || !dominoMotivationNeedsTask(motivation, human->data) || needs.energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        *outcome = DOMINO_PLAN_UNCHANGED;
        return nullptr;
    }
    if (motivation->work_round == UINT64_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "sleep work round exhausted");
    DominoTaskCandidate* candidate = dominoHumanAllocateCandidate(sizeof(*candidate) + sizeof(candidate->items[0]), 1, 0);
    candidate->items[0] = (DominoTaskSuggestion){.motivation_id = motivation->motivation_id,
                                                 .work_round = motivation->work_round + 1,
                                                 .type = DOMINO_TASK_TYPE_SLEEP,
                                                 .target_energy = DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY,
                                                 .expected_queue_version = human->runtime->channel_version[0],
                                                 .queue_position = human->data->queue_count[0]};
    *outcome = DOMINO_PLAN_CANDIDATE;
    return &candidate->header;
}
DominoPlanCandidate* dominoHumanPlanBehaviors(const DominoHuman* human, DominoPlanScope scope, uint32_t now, uint32_t budget,
                                              DominoPlanOutcome* outcome) {
    *outcome = DOMINO_PLAN_UNAVAILABLE;
    const DominoTask* task = dominoGetTaskByID(scope.object_id);
    if (!task || !dominoTaskQuery(task, human->data, human->data->settled_at_ms).needs_behavior || !human->data->sleep_allowed) {
        *outcome = DOMINO_PLAN_UNCHANGED;
        return nullptr;
    }
    if (task->type != DOMINO_TASK_TYPE_SLEEP)
        return nullptr;
    DominoNeedValues needs;
    if (!dominoHumanPredictNeeds(human, now, &budget, &needs))
        return nullptr;
    DominoBehavior simulation = {.plan = {UINT32_MAX, DOMINO_BEHAVIOR_TYPE_SLEEP}, .status = DOMINO_BEHAVIOR_STATUS_ACTIVE};
    uint32_t base = now;
    // 有限计算预算内沿共用基础边界找到首次达标时长，包含自然需求相位。
    while (needs.energy < task->target_energy && budget--) {
        uint32_t delta = dominoBehaviorNextDelta(&simulation);
        uint32_t natural = DOMINO_HUMAN_NEED_MINUTE_MS - needs.phase_ms;
        if (natural < delta)
            delta = natural;
        uint32_t end = dominoHumanTimeAdd(base, delta);
        needs = dominoHumanCalculateNeeds(needs, base, end);
        dominoBehaviorCalculate(&simulation, &needs, base, end);
        base = end;
    }
    if (needs.energy < task->target_energy || base == now)
        return nullptr;
    size_t prefix = sizeof(DominoBehaviorCandidate) + sizeof(DominoBehaviorSuggestion);
    DominoBehaviorCandidate* candidate = dominoHumanAllocateCandidate(prefix + sizeof(DominoBehaviorPlan), 1, 1);
    candidate->items[0] = (DominoBehaviorSuggestion){.expected_plan_version = task->plan_version,
                                                     .steps_offset = (uint32_t)prefix,
                                                     .task_id = task->task_id,
                                                     .step_count = 1,
                                                     .replace_plans = true,
                                                     .depends_on_behavior = true,
                                                     .expected_behavior_id = task->execution.current_behavior_id,
                                                     .requires_unstarted = !task->has_execution};
    *(DominoBehaviorPlan*)((char*)candidate + prefix) = (DominoBehaviorPlan){dominoHumanTimeSpan(now, base), DOMINO_BEHAVIOR_TYPE_SLEEP};
    *outcome = DOMINO_PLAN_CANDIDATE;
    return &candidate->header;
}

DominoPlanPriority dominoHumanPlanningPriority(const DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope) {
    if (human->data->energy <= 10 || scope.accepted_task)
        return DOMINO_PLAN_HIGH;
    if (kind == DOMINO_PLANNER_BEHAVIOR) {
        const DominoTask* task = dominoGetTaskByID(scope.object_id);
        if (task && task->has_execution)
            return DOMINO_PLAN_HIGH;
    }
    return DOMINO_PLAN_NORMAL;
}
