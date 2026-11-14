#include "entry.h"

#include "../behavior/entry.h"
#include "../motivation/entry.h"
#include "../needs.h"
#include "../rules.h"
#include "../store.h"
#include "sleep.h"
#ifdef DOMINO_PLAYGROUND_MODE
bool dominoTaskFixtureSupported(DOMINO_TASK_TYPE type);
DominoTaskDecision dominoTaskFixtureDecide(const DominoTask* task, DominoTaskDecisionInput input);
#endif
bool dominoTaskSupported(DOMINO_TASK_TYPE type) {
    if (type == DOMINO_TASK_TYPE_SLEEP)
        return true;
#ifdef DOMINO_PLAYGROUND_MODE
    return dominoTaskFixtureSupported(type);
#else
    return false;
#endif
}
bool dominoTaskDependenciesReady(const DominoTask* task, uint8_t stage) {
    for (uint8_t i = 0; i < task->dependency_count; ++i) {
        const DominoTaskDependency* dependency = &task->dependencies[i];
        if (!(dependency->stages & stage))
            continue;
        if (dependency->resolved_result) {
            if (dependency->resolved_result == 1)
                continue;
            return false;
        }
        const DominoTask* source = dominoGetTaskByID(dependency->task_id);
        if (!source || !source->has_execution)
            return false;
        if (dependency->condition == DOMINO_DEPENDENCY_SUCCEEDED) {
            if (source->execution.status != DOMINO_TASK_STATUS_SUCCEEDED)
                return false;
        } else if (source->execution.status != DOMINO_TASK_STATUS_OPEN || !source->execution.current_behavior_id ||
                   dominoGetBehaviorByID(source->execution.current_behavior_id)->status != DOMINO_BEHAVIOR_STATUS_ACTIVE)
            return false;
    }
    return true;
}
DominoTaskDecision dominoTaskDecide(const DominoTask* task, DominoTaskDecisionInput input) {
    if (task->has_execution && task->execution.status != DOMINO_TASK_STATUS_OPEN)
        return (DominoTaskDecision){DOMINO_TASK_CONTINUE, 0, false};
    DominoTaskDecision decision;
    if (task->type == DOMINO_TASK_TYPE_SLEEP)
        decision = dominoSleepTaskDecide(task, input);
#ifdef DOMINO_PLAYGROUND_MODE
    else
        decision = dominoTaskFixtureDecide(task, input);
#else
    else
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "unsupported task decision");
#endif
    if (!input.dependencies_ready && decision.action != DOMINO_TASK_CANCEL && decision.action != DOMINO_TASK_REVOKE)
        decision = (DominoTaskDecision){DOMINO_TASK_WAIT, 3, true};
    bool complete = decision.action == DOMINO_TASK_SUCCEED && input.completion_ready;
    if (decision.action == DOMINO_TASK_SUCCEED && !complete)
        decision = (DominoTaskDecision){DOMINO_TASK_WAIT, 3, false};
    if (task->finish_deadline.valid &&
        (input.now_ms > task->finish_deadline.at_ms || (input.now_ms == task->finish_deadline.at_ms && (!task->finish_inclusive || !complete)))) {
        decision = (DominoTaskDecision){task->has_execution ? DOMINO_TASK_FAIL : DOMINO_TASK_REVOKE, 4, true};
    }
    if (task->start_deadline.valid && !task->has_execution && input.now_ms >= task->start_deadline.at_ms &&
        (decision.action == DOMINO_TASK_CONTINUE || decision.action == DOMINO_TASK_WAIT || decision.action == DOMINO_TASK_REPAIR))
        decision = (DominoTaskDecision){DOMINO_TASK_REPAIR, 5, false};
    return decision;
}
void dominoTaskFinishBehavior(DominoTask* task, DOMINO_BEHAVIOR_STATUS status, domino_runtime_ms_t now, uint32_t reason) {
    DominoBehavior* behavior = dominoGetBehaviorByID(task->execution.current_behavior_id);
    if (!behavior || behavior->status >= DOMINO_BEHAVIOR_STATUS_SUCCEEDED)
        return;
    if (UINT32_MAX - task->execution.completed_active_ms < behavior->active_elapsed_ms)
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "task progress overflow");
    task->execution.completed_active_ms += behavior->active_elapsed_ms;
    if (status == DOMINO_BEHAVIOR_STATUS_SUCCEEDED) {
        if (task->execution.successful_behavior_count == UINT64_MAX)
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "behavior success count overflow");
        ++task->execution.successful_behavior_count;
    }
    task->execution.last_reason = reason;
    dominoBehaviorFinish(behavior, status, now);
    task->execution.current_behavior_id = 0;
}
void dominoTaskCheckBehavior(DominoTask* task, domino_runtime_ms_t now) {
    const DominoBehavior* behavior = dominoGetBehaviorByID(task->execution.current_behavior_id);
    if (!behavior)
        return;
    DOMINO_BEHAVIOR_STATUS status = dominoBehaviorDecide(behavior);
    if (status >= DOMINO_BEHAVIOR_STATUS_SUCCEEDED)
        dominoTaskFinishBehavior(task, status, now, 0);
}
void dominoTaskApplyDecision(DominoTask* task, DominoTaskDecision decision, domino_runtime_ms_t now) {
    if (task->has_execution && task->execution.status != DOMINO_TASK_STATUS_OPEN)
        return;
    if (decision.cancel_behavior)
        dominoTaskFinishBehavior(task, DOMINO_BEHAVIOR_STATUS_CANCELLED, now, decision.reason);
    task->waiting_condition = decision.action == DOMINO_TASK_WAIT;
    if (decision.reason == 2 || decision.reason == 5) {
        if (task->plan_count)
            dominoHumanStoreReplacePlans(task, nullptr, 0);
    }
    if (decision.action != DOMINO_TASK_SUCCEED && decision.action != DOMINO_TASK_FAIL && decision.action != DOMINO_TASK_CANCEL &&
        decision.action != DOMINO_TASK_REVOKE)
        return;
    if (decision.action == DOMINO_TASK_SUCCEED && !task->execution.successful_behavior_count)
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "task success without own behavior proof");
    if (task->plan_count)
        dominoHumanStoreReplacePlans(task, nullptr, 0);
    if (task->has_execution) {
        task->execution.status = decision.action == DOMINO_TASK_SUCCEED ? DOMINO_TASK_STATUS_SUCCEEDED
                                 : decision.action == DOMINO_TASK_FAIL  ? DOMINO_TASK_STATUS_FAILED
                                                                        : DOMINO_TASK_STATUS_CANCELLED;
        task->execution.ended_at_ms = now;
        task->execution.last_reason = decision.reason;
    }
    DominoMotivation* motivation = dominoGetMotivationByID(task->motivation_id);
    if (decision.action == DOMINO_TASK_SUCCEED)
        dominoMotivationRecordSuccess(motivation, task);
    dominoMotivationCloseWork(
        motivation, task, decision.action == DOMINO_TASK_FAIL || (decision.action == DOMINO_TASK_REVOKE && decision.reason == 4), decision.reason);
}
void dominoTaskSettleEffects(DominoTask* task, DominoHumanData* data, domino_runtime_ms_t begin, domino_runtime_ms_t end) {
    DominoBehavior* behavior = dominoGetBehaviorByID(task->execution.current_behavior_id);
    if (!behavior)
        return;
    DominoNeedValues needs = dominoHumanReadNeeds(data);
    dominoBehaviorCalculate(behavior, &needs, begin, end);
    dominoHumanWriteNeeds(data, needs);
}
DominoTaskSummary dominoTaskQuery(const DominoTask* task, const DominoHumanData* data, domino_runtime_ms_t base) {
    DominoTaskSummary result = {.task_key = task->type, .success_proof = task->execution.successful_behavior_count != 0};
    if (task->has_execution && task->execution.status != DOMINO_TASK_STATUS_OPEN)
        return result;
    result.needs_arrangement = !task->has_execution && task->start_deadline.valid && base >= task->start_deadline.at_ms;
    const DominoBehavior* behavior = dominoGetBehaviorByID(task->execution.current_behavior_id);
    if (behavior) {
        result.behavior_key = behavior->plan.type;
        result.holds_behavior = behavior->status < DOMINO_BEHAVIOR_STATUS_SUCCEEDED;
        result.running = behavior->status == DOMINO_BEHAVIOR_STATUS_ACTIVE;
        if (result.running) {
            uint32_t delta = dominoBehaviorNextDelta(behavior);
            result.next = (DominoHumanCheckTime){dominoHumanTimeAdd(base, delta), true};
        }
    } else {
        result.needs_behavior = task->plan_count == 0 && !task->waiting_condition && !result.needs_arrangement;
        result.can_start = task->plan_count && dominoBehaviorCanStart(&task->plans[0], data) &&
                           (!task->not_before.valid || base >= task->not_before.at_ms) &&
                           (!task->start_deadline.valid || task->has_execution || base < task->start_deadline.at_ms) &&
                           dominoTaskDependenciesReady(task, task->has_execution ? DOMINO_DEPENDENCY_CONTINUE : DOMINO_DEPENDENCY_START);
        if (task->plan_count)
            result.behavior_key = task->plans[0].type;
    }
    return result;
}
void dominoTaskStartBehavior(DominoTask* task, domino_runtime_ms_t now) {
    if (!task->has_execution) {
        task->has_execution = true;
        task->execution.status = DOMINO_TASK_STATUS_OPEN;
        task->execution.started_at_ms = now;
    }
    DominoBehavior value = {.plan = task->plans[0], .started_at_ms = now, .status = DOMINO_BEHAVIOR_STATUS_ACTIVE};
    // 建立记录和消费计划在同一次写入完成；只消费正式启动的步骤。
    task->execution.current_behavior_id = dominoHumanStoreAddBehavior(task, value);
    dominoHumanStoreConsumePlan(task);
    task->waiting_condition = false;
}
