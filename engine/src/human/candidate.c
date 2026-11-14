#include "candidate.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "advance.h"
#include "behavior/entry.h"
#include "motivation/entry.h"
#include "rules.h"
#include "runtime.h"
#include "state.h"
#include "store.h"
#include "task/entry.h"

static _Atomic size_t g_candidate_bytes;
static _Atomic size_t g_candidate_peak;
static _Atomic uint64_t g_allocations, g_allocated_bytes, g_allocation_ns;
DominoCandidateMetrics dominoHumanCandidateMetrics(void) {
    return (DominoCandidateMetrics){atomic_load(&g_allocations), atomic_load(&g_allocated_bytes), atomic_load(&g_allocation_ns)};
}
size_t dominoHumanCandidateLiveBytes(void) {
    return atomic_load(&g_candidate_bytes);
}
size_t dominoHumanCandidatePeakBytes(void) {
    return atomic_load(&g_candidate_peak);
}
void dominoHumanCandidateResetMetrics(void) {
    if (atomic_load(&g_candidate_bytes))
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "candidate ownership leak");
    atomic_store(&g_candidate_peak, 0);
    atomic_store(&g_allocations, 0);
    atomic_store(&g_allocated_bytes, 0);
    atomic_store(&g_allocation_ns, 0);
}
void dominoHumanFreeCandidate(DominoPlanCandidate* candidate) {
    if (!candidate)
        return;
    atomic_fetch_sub(&g_candidate_bytes, candidate->bytes);
    free(candidate);
}
void* dominoHumanAllocateCandidate(size_t bytes, uint16_t items, uint16_t steps) {
    uint64_t start = dominoMonotonicTimeNs();
    if (bytes > DOMINO_PLAN_BYTES_MAX || bytes < sizeof(DominoPlanCandidate))
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "candidate allocation size");
    size_t old = atomic_fetch_add(&g_candidate_bytes, bytes);
    if (old > 256U * 1024U * 1024U - bytes)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "candidate memory budget exhausted");
    size_t peak = atomic_load(&g_candidate_peak);
    while (peak < old + bytes && !atomic_compare_exchange_weak(&g_candidate_peak, &peak, old + bytes)) {
    }
    DominoPlanCandidate* result = calloc(1, bytes);
    if (!result)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate candidate");
    *result = (DominoPlanCandidate){(uint32_t)bytes, items, steps};
    atomic_fetch_add(&g_allocations, 1);
    atomic_fetch_add(&g_allocated_bytes, bytes);
    atomic_fetch_add(&g_allocation_ns, dominoMonotonicTimeNs() - start);
    return result;
}
static bool openTask(const DominoTask* task) {
    return task && (!task->has_execution || task->execution.status == DOMINO_TASK_STATUS_OPEN);
}
static bool acceptMotivations(DominoHuman* human, DominoPlanScope scope, const DominoMotivationCandidate* candidate) {
    uint16_t count = candidate->header.item_count;
    if (candidate->header.bytes != sizeof(*candidate) + count * sizeof(candidate->items[0]) || candidate->header.step_count)
        return false;
    bool selected[DOMINO_PLAN_ITEM_MAX] = {0}, covers = false;
    uint32_t added = 0;
    for (uint8_t pass = 0; pass < 2; ++pass)
        for (uint16_t i = 0; i < count; ++i) {
            const DominoMotivationSuggestion* item = &candidate->items[i];
            if (item->optional != (pass == 1))
                continue;
            bool valid =
                item->type == DOMINO_MOTIVATION_RECOVER_ENERGY && item->needs_action == (human->data->energy <= DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY);
            const DominoMotivation* motivation = dominoGetMotivationByID(item->motivation_id);
            if (item->motivation_id)
                valid &= motivation && motivation->owner_human_id == human->human_id && motivation->type == item->type &&
                         motivation->status == DOMINO_MOTIVATION_VALID;
            else {
                for (uint8_t j = 0; j < human->data->motivation_count; ++j) {
                    const DominoMotivation* existing = dominoGetMotivationByID(human->data->motivation_ids[j]);
                    if (existing->type == item->type && existing->status == DOMINO_MOTIVATION_VALID)
                        valid = false;
                }
                valid &= human->data->motivation_count + added < DOMINO_HUMAN_MOTIVATION_COUNT_MAX &&
                         kv_size(domino_all_motivation_list) + added < DOMINO_MOTIVATION_POOL_MAX;
            }
            for (uint16_t j = 0; j < count; ++j)
                if (j != i && selected[j] && candidate->items[j].motivation_id == item->motivation_id)
                    valid = false;
            if (!valid && !item->optional)
                return false;
            if (!valid)
                continue;
            selected[i] = true;
            if (!item->motivation_id)
                ++added;
            if (!item->optional && item->motivation_id == scope.object_id)
                covers = true;
        }
    if (!covers)
        return false;
    for (uint16_t i = 0; i < count; ++i)
        if (selected[i]) {
            const DominoMotivationSuggestion* item = &candidate->items[i];
            DominoMotivation* motivation = dominoGetMotivationByID(item->motivation_id);
            if (!motivation) {
                dominoHumanStoreAddMotivation(
                    human->data, (DominoMotivation){.type = item->type, .status = DOMINO_MOTIVATION_VALID, .needs_action = item->needs_action});
            } else {
                motivation->needs_action = item->needs_action;
                motivation->reassess = false;
            }
        }
    return true;
}

/** @brief 本次预检的最终任务值和通道位置；不修改正式容器。 */
typedef struct TaskArrangement {
    DominoTask tasks[DOMINO_HUMAN_TASK_COUNT_MAX];
    uint8_t queues[DOMINO_HUMAN_TASK_CHANNEL_COUNT][DOMINO_HUMAN_TASK_COUNT_MAX];
    uint8_t queue_count[DOMINO_HUMAN_TASK_CHANNEL_COUNT];
    uint8_t count;
    uint8_t local[DOMINO_PLAN_ITEM_MAX];
    bool selected[DOMINO_PLAN_ITEM_MAX];
    bool fresh[DOMINO_PLAN_ITEM_MAX];
    bool cancelled[DOMINO_HUMAN_TASK_COUNT_MAX];
} TaskArrangement;
static int findTask(const TaskArrangement* final, uint32_t id) {
    for (uint8_t i = 0; i < final->count; ++i)
        if (final->tasks[i].task_id == id)
            return i;
    return -1;
}
static bool visitGraph(uint8_t index, const uint32_t* edges, uint8_t* marks, uint8_t count) {
    if (marks[index] == 1)
        return false;
    if (marks[index] == 2)
        return true;
    marks[index] = 1;
    for (uint8_t j = 0; j < count; ++j)
        if ((edges[index] & (UINT32_C(1) << j)) && !visitGraph(j, edges, marks, count))
            return false;
    marks[index] = 2;
    return true;
}
static bool checkArrangement(TaskArrangement* final, const DominoTaskCandidate* candidate) {
    uint32_t edges[DOMINO_HUMAN_TASK_COUNT_MAX] = {0};
    uint8_t marks[DOMINO_HUMAN_TASK_COUNT_MAX] = {0};
    for (uint8_t t = 0; t < final->count; ++t) {
        const DominoTask* task = &final->tasks[t];
        for (uint8_t d = 0; d < task->dependency_count; ++d) {
            const DominoTaskDependency* dep = &task->dependencies[d];
            if (dep->resolved_result)
                continue;
            int source = findTask(final, dep->task_id);
            for (uint16_t i = 0; i < candidate->header.item_count; ++i)
                if (final->selected[i] && final->local[i] == t) {
                    uint16_t local = candidate->items[i].dependency_local[d];
                    if (local != DOMINO_PLAN_LOCAL_NONE) {
                        if (local >= candidate->header.item_count || !final->selected[local] || candidate->items[local].optional)
                            return false;
                        source = final->local[local];
                    }
                }
            if (source < 0 || source == t || final->cancelled[source])
                return false;
            if (dep->condition == DOMINO_DEPENDENCY_ACTIVE && task->type == final->tasks[source].type)
                return false;
            edges[t] |= UINT32_C(1) << source;
            // 同队列后方工作不能成为队首必须等待的来源。
            if (task->channel_index == final->tasks[source].channel_index) {
                int target_pos = -1, source_pos = -1;
                uint8_t c = task->channel_index;
                for (uint8_t q = 0; q < final->queue_count[c]; ++q) {
                    if (final->queues[c][q] == t)
                        target_pos = q;
                    if (final->queues[c][q] == source)
                        source_pos = q;
                }
                if (source_pos >= 0 && (target_pos < 0 || source_pos > target_pos))
                    return false;
            }
        }
    }
    for (uint8_t t = 0; t < final->count; ++t)
        if (!visitGraph(t, edges, marks, final->count))
            return false;
    return true;
}
static bool tryTask(DominoHuman* human, const DominoTaskCandidate* candidate, uint16_t index, TaskArrangement* final) {
    const DominoTaskSuggestion* item = &candidate->items[index];
    if (item->channel_index >= DOMINO_HUMAN_TASK_CHANNEL_COUNT || item->dependency_count > DOMINO_TASK_DEPENDENCY_MAX ||
        item->target_energy > DOMINO_HUMAN_NEED_MAX || !dominoTaskSupported(item->type) ||
        item->expected_queue_version != human->runtime->channel_version[item->channel_index])
        return false;
    const DominoMotivation* motivation = dominoGetMotivationByID(item->motivation_id);
    if (!motivation || motivation->owner_human_id != human->human_id)
        return false;
    for (uint8_t d = 0; d < item->dependency_count; ++d) {
        const DominoTaskDependency* dep = &item->dependencies[d];
        if (!dep->stages || dep->stages > 7 || dep->condition > DOMINO_DEPENDENCY_ACTIVE)
            return false;
        if (dep->resolved_result) {
            const DominoTask* old = dominoGetTaskByID(item->task_id);
            bool retained = false;
            for (uint8_t j = 0; old && j < old->dependency_count; ++j) {
                const DominoTaskDependency* prior = &old->dependencies[j];
                retained |= prior->task_id == dep->task_id && prior->condition == dep->condition && prior->stages == dep->stages &&
                            prior->resolved_result == dep->resolved_result;
            }
            if (!retained || item->dependency_local[d] != DOMINO_PLAN_LOCAL_NONE)
                return false;
        }
        if (item->dependency_local[d] != DOMINO_PLAN_LOCAL_NONE &&
            (item->dependency_local[d] >= candidate->header.item_count || candidate->items[item->dependency_local[d]].optional))
            return false;
    }
    int slot = item->task_id ? findTask(final, item->task_id) : -1;
    bool fresh = !item->task_id;
    if (fresh) {
        // 重复建议复用原记录，不能用初始安排覆盖后来已经接受的事实。
        for (uint8_t i = 0; i < final->count; ++i) {
            if (final->tasks[i].motivation_id == item->motivation_id && final->tasks[i].work_round == item->work_round) {
                if (final->tasks[i].type != item->type || final->tasks[i].channel_index != item->channel_index ||
                    final->tasks[i].target_energy != item->target_energy || final->tasks[i].finish_deadline.valid != item->finish_deadline.valid ||
                    final->tasks[i].finish_deadline.at_ms != item->finish_deadline.at_ms ||
                    final->tasks[i].finish_inclusive != item->finish_inclusive)
                    return false;
                final->local[index] = i;
                final->selected[index] = true;
                return true;
            }
        }
        if (motivation->status != DOMINO_MOTIVATION_VALID || motivation->work_round == UINT64_MAX || item->work_round != motivation->work_round + 1 ||
            !dominoMotivationNeedsTask(motivation, human->data) || final->count == DOMINO_HUMAN_TASK_COUNT_MAX ||
            kv_size(domino_all_task_list) + final->count - human->data->task_count >= DOMINO_TASK_POOL_MAX || item->cancel || item->yield_current)
            return false;
        slot = final->count++;
        final->tasks[slot] = (DominoTask){.motivation_id = item->motivation_id,
                                          .work_round = item->work_round,
                                          .owner_human_id = human->human_id,
                                          .type = item->type,
                                          .channel_index = item->channel_index,
                                          .target_energy = item->target_energy};
    } else {
        if (slot < 0)
            return false;
        DominoTask* old = &final->tasks[slot];
        if (!openTask(old) || old->owner_human_id != human->human_id || old->arrangement_version != item->expected_arrangement_version ||
            old->motivation_id != item->motivation_id || old->work_round != item->work_round || old->type != item->type ||
            old->channel_index != item->channel_index || old->target_energy != item->target_energy ||
            old->finish_deadline.valid != item->finish_deadline.valid || old->finish_deadline.at_ms != item->finish_deadline.at_ms ||
            old->finish_inclusive != item->finish_inclusive)
            return false;
        for (uint16_t j = 0; j < candidate->header.item_count; ++j)
            if (j != index && final->selected[j] && final->local[j] == slot)
                return false;
        if (human->data->active_task_id_list[item->channel_index] == item->task_id && !item->yield_current && !item->cancel)
            return false;
        if (item->yield_current && (!old->has_execution || human->data->active_task_id_list[item->channel_index] != item->task_id))
            return false;
    }
    final->cancelled[slot] = item->cancel;
    DominoTask* target = &final->tasks[slot];
    target->dependency_count = item->dependency_count;
    memcpy(target->dependencies, item->dependencies, sizeof(target->dependencies));
    target->not_before = item->not_before;
    target->start_deadline = item->start_deadline;
    target->finish_deadline = item->finish_deadline;
    target->finish_inclusive = item->finish_inclusive;
    uint8_t c = item->channel_index;
    for (uint8_t q = 0; q < final->queue_count[c]; ++q)
        if (final->queues[c][q] == slot) {
            --final->queue_count[c];
            memmove(&final->queues[c][q], &final->queues[c][q + 1], final->queue_count[c] - q);
            break;
        }
    if (!item->cancel) {
        uint8_t pos = item->queue_position;
        if (pos > final->queue_count[c])
            return false;
        memmove(&final->queues[c][pos + 1], &final->queues[c][pos], final->queue_count[c] - pos);
        final->queues[c][pos] = (uint8_t)slot;
        ++final->queue_count[c];
    }
    final->local[index] = (uint8_t)slot;
    final->selected[index] = true;
    final->fresh[index] = fresh;
    return true;
}
static bool acceptTasks(DominoHuman* human, DominoPlanScope scope, const DominoTaskCandidate* candidate, uint32_t now) {
    uint16_t count = candidate->header.item_count;
    if (candidate->header.bytes != sizeof(*candidate) + count * sizeof(candidate->items[0]) || candidate->header.step_count)
        return false;
    for (uint16_t i = 0; i < count; ++i) {
        const DominoTaskSuggestion* item = &candidate->items[i];
        if (item->dependency_count > DOMINO_TASK_DEPENDENCY_MAX)
            return false;
        for (uint8_t d = 0; d < item->dependency_count; ++d) {
            uint16_t local = item->dependency_local[d];
            if (local != DOMINO_PLAN_LOCAL_NONE && (local >= count || candidate->items[local].optional))
                return false;
        }
    }
    TaskArrangement final = {.count = human->data->task_count};
    for (uint8_t i = 0; i < final.count; ++i)
        final.tasks[i] = *dominoGetTaskByID(human->data->task_id_list[i]);
    for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c) {
        final.queue_count[c] = human->data->queue_count[c];
        for (uint8_t q = 0; q < final.queue_count[c]; ++q)
            final.queues[c][q] = (uint8_t)findTask(&final, human->data->channel_queue[c][q]);
    }
    bool covers = false;
    for (uint16_t i = 0; i < count; ++i)
        if (!candidate->items[i].optional) {
            if (!tryTask(human, candidate, i, &final))
                return false;
            covers |= scope.accepted_task ? candidate->items[i].task_id == scope.object_id : candidate->items[i].motivation_id == scope.object_id;
        }
    if (!covers || !checkArrangement(&final, candidate))
        return false;
    for (uint16_t i = 0; i < count; ++i)
        if (candidate->items[i].optional) {
            TaskArrangement attempt = final;
            if (tryTask(human, candidate, i, &attempt) && checkArrangement(&attempt, candidate))
                final = attempt;
        }
    // 所有可恢复检查已完成。先分配正式任务身份，再解析候选内关联。
    for (uint16_t i = 0; i < count; ++i)
        if (final.selected[i] && final.fresh[i]) {
            DominoTask value = final.tasks[final.local[i]];
            value.dependency_count = 0;
            uint32_t id = dominoHumanStoreAddTask(human->data, value);
            final.tasks[final.local[i]].task_id = id;
            DominoMotivation* motivation = dominoGetMotivationByID(value.motivation_id);
            motivation->work_round = value.work_round;
        }
    for (uint16_t i = 0; i < count; ++i)
        if (final.selected[i]) {
            const DominoTaskSuggestion* item = &candidate->items[i];
            DominoTask* task = dominoGetTaskByID(final.tasks[final.local[i]].task_id);
            if (!final.fresh[i] && !item->task_id)
                continue;
            if (!final.fresh[i])
                dominoHumanStoreAdvanceArrangement(task);
            for (uint8_t d = 0; d < item->dependency_count; ++d) {
                task->dependencies[d] = item->dependencies[d];
                uint16_t local = item->dependency_local[d];
                if (local != DOMINO_PLAN_LOCAL_NONE)
                    task->dependencies[d].task_id = final.tasks[final.local[local]].task_id;
            }
            task->dependency_count = item->dependency_count;
            task->not_before = item->not_before;
            task->start_deadline = item->start_deadline;
            task->finish_deadline = item->finish_deadline;
            task->finish_inclusive = item->finish_inclusive;
            if (item->yield_current || item->cancel) {
                dominoTaskFinishBehavior(task, DOMINO_BEHAVIOR_STATUS_CANCELLED, now, 6);
                if (human->data->active_task_id_list[item->channel_index] == task->task_id)
                    human->data->active_task_id_list[item->channel_index] = 0;
            }
            if (item->cancel)
                dominoTaskApplyDecision(task, (DominoTaskDecision){task->has_execution ? DOMINO_TASK_CANCEL : DOMINO_TASK_REVOKE, 6, true}, now);
        }
    for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c) {
        bool changed = human->data->queue_count[c] != final.queue_count[c];
        for (uint8_t q = 0; q < final.queue_count[c]; ++q)
            if (human->data->channel_queue[c][q] != final.tasks[final.queues[c][q]].task_id)
                changed = true;
        if (changed) {
            while (human->data->queue_count[c])
                dominoHumanQueueRemove(human, human->data->channel_queue[c][0], c);
            for (uint8_t q = 0; q < final.queue_count[c]; ++q)
                dominoHumanQueueInsert(human, final.tasks[final.queues[c][q]].task_id, c, q);
        }
    }
    for (uint16_t i = 0; i < count; ++i)
        if (final.selected[i] && candidate->items[i].cancel) {
            uint32_t id = final.tasks[final.local[i]].task_id;
            DominoTask* task = dominoGetTaskByID(id);
            if (!task->has_execution)
                dominoHumanResolveTaskReferences(human->data, task, false);
            if (!task->has_execution)
                for (uint8_t j = 0; j < human->data->task_count; ++j)
                    if (human->data->task_id_list[j] == id) {
                        dominoHumanStoreRecycleTask(human->data, j);
                        break;
                    }
        }
    return true;
}
static bool acceptBehaviors(DominoHuman* human, DominoPlanScope scope, const DominoBehaviorCandidate* candidate, uint32_t now) {
    uint16_t count = candidate->header.item_count;
    size_t prefix = sizeof(*candidate) + count * sizeof(candidate->items[0]);
    if (candidate->header.bytes != prefix + candidate->header.step_count * sizeof(DominoBehaviorPlan))
        return false;
    // 结构规模和候选内引用错误使整份载荷无效，不能伪装成可省略的业务失败。
    for (uint16_t i = 0; i < count; ++i) {
        const DominoBehaviorSuggestion* item = &candidate->items[i];
        if (!item->replace_plans) {
            if (item->step_count)
                return false;
            continue;
        }
        if (item->steps_offset < prefix || item->steps_offset % _Alignof(DominoBehaviorPlan) || item->steps_offset > candidate->header.bytes ||
            item->step_count > DOMINO_PLAN_STEP_MAX ||
            (size_t)item->step_count * sizeof(DominoBehaviorPlan) > candidate->header.bytes - item->steps_offset)
            return false;
    }
    bool selected[DOMINO_PLAN_ITEM_MAX] = {0}, covers = false;
    size_t plans = dominoHumanStorePlanCount();
    // 必要部分先预检，随后可选项按原次序尝试；不允许同一对象出现多次。
    for (uint8_t pass = 0; pass < 2; ++pass)
        for (uint16_t i = 0; i < count; ++i) {
            const DominoBehaviorSuggestion* item = &candidate->items[i];
            if (item->optional != (pass == 1))
                continue;
            const DominoTask* task = dominoGetTaskByID(item->task_id);
            bool valid = openTask(task) && task->owner_human_id == human->human_id && task->plan_version == item->expected_plan_version &&
                         (!item->requires_unstarted || !task->has_execution) &&
                         (!item->depends_on_behavior || task->execution.current_behavior_id == item->expected_behavior_id);
            if (valid && item->replace_plans) {
                valid &= task->record_count + item->step_count <= DOMINO_TASK_BEHAVIOR_COUNT_MAX &&
                         plans - task->plan_count + item->step_count <= DOMINO_BEHAVIOR_PLAN_POOL_MAX;
                const DominoBehaviorPlan* steps = (const void*)((const char*)candidate + item->steps_offset);
                for (uint16_t j = 0; valid && j < item->step_count; ++j)
                    valid = dominoBehaviorPlanValid(&steps[j]);
            }
            if (valid && item->adjust_current) {
                const DominoBehavior* behavior = dominoGetBehaviorByID(task->execution.current_behavior_id);
                valid = item->depends_on_behavior && behavior && behavior->status < DOMINO_BEHAVIOR_STATUS_SUCCEEDED &&
                        behavior->plan.duration_ms == item->expected_duration_ms && item->adjusted_duration_ms >= behavior->active_elapsed_ms &&
                        item->adjusted_duration_ms > 0;
            }
            for (uint16_t j = 0; j < count; ++j)
                if (j != i && selected[j] && candidate->items[j].task_id == item->task_id)
                    valid = false;
            if (!valid && !item->optional)
                return false;
            if (!valid)
                continue;
            selected[i] = true;
            if (item->replace_plans)
                plans = plans - task->plan_count + item->step_count;
            covers |= !item->optional && item->task_id == scope.object_id;
        }
    if (!covers)
        return false;
    for (uint16_t i = 0; i < count; ++i)
        if (selected[i]) {
            const DominoBehaviorSuggestion* item = &candidate->items[i];
            DominoTask* task = dominoGetTaskByID(item->task_id);
            if (item->replace_plans)
                dominoHumanStoreReplacePlans(task, (const void*)((const char*)candidate + item->steps_offset), item->step_count);
            if (item->adjust_current)
                dominoGetBehaviorByID(task->execution.current_behavior_id)->plan.duration_ms = item->adjusted_duration_ms;
            task->retry.valid = false;
            dominoTaskCheckBehavior(task, now);
        }
    return true;
}
bool dominoHumanAcceptCandidate(DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope, const DominoPlanCandidate* candidate,
                                uint32_t now) {
    if (!candidate || candidate->bytes < sizeof(*candidate) || candidate->bytes > DOMINO_PLAN_BYTES_MAX || !candidate->item_count ||
        candidate->item_count > DOMINO_PLAN_ITEM_MAX || candidate->step_count > DOMINO_PLAN_STEP_MAX)
        return false;
    switch (kind) {
        case DOMINO_PLANNER_MOTIVATION:
            return acceptMotivations(human, scope, (const void*)candidate);
        case DOMINO_PLANNER_TASK:
            return acceptTasks(human, scope, (const void*)candidate, now);
        case DOMINO_PLANNER_BEHAVIOR:
            return acceptBehaviors(human, scope, (const void*)candidate, now);
        default:
            return false;
    }
}
