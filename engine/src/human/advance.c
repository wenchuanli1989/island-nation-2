#include "advance.h"

#include <string.h>

#include "behavior/entry.h"
#include "rules.h"
#include "runtime.h"
#include "state.h"
#include "store.h"
#include "task/entry.h"

static void bumpQueue(DominoHuman* human, uint8_t channel) {
    uint64_t* version = &human->runtime->channel_version[channel];
    if (*version == UINT64_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "channel version exhausted");
    ++*version;
}
void dominoHumanQueueInsert(DominoHuman* human, domino_task_id_t id, uint8_t channel, uint8_t position) {
    DominoHumanData* data = human->data;
    uint8_t count = data->queue_count[channel];
    memmove(&data->channel_queue[channel][position + 1], &data->channel_queue[channel][position], (count - position) * sizeof(id));
    data->channel_queue[channel][position] = id;
    ++data->queue_count[channel];
    bumpQueue(human, channel);
}
void dominoHumanQueueRemove(DominoHuman* human, domino_task_id_t id, uint8_t channel) {
    DominoHumanData* data = human->data;
    for (uint8_t i = 0; i < data->queue_count[channel]; ++i) {
        if (data->channel_queue[channel][i] != id)
            continue;
        --data->queue_count[channel];
        memmove(&data->channel_queue[channel][i], &data->channel_queue[channel][i + 1], (data->queue_count[channel] - i) * sizeof(id));
        data->channel_queue[channel][data->queue_count[channel]] = 0;
        bumpQueue(human, channel);
        break;
    }
}
/** @brief 有界 DFS：依赖先于使用者，同一边界不先失败再恢复。 */
static void orderTask(const DominoHumanData* data, uint8_t index, uint8_t* marks, uint32_t* order, uint8_t* count) {
    if (marks[index] == 2)
        return;
    if (marks[index] == 1)
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "cyclic accepted tasks");
    marks[index] = 1;
    const DominoTask* task = dominoGetTaskByID(data->task_id_list[index]);
    for (uint8_t d = 0; d < task->dependency_count; ++d) {
        if (task->dependencies[d].resolved_result)
            continue;
        for (uint8_t j = 0; j < data->task_count; ++j)
            if (data->task_id_list[j] == task->dependencies[d].task_id)
                orderTask(data, j, marks, order, count);
    }
    marks[index] = 2;
    order[(*count)++] = task->task_id;
}
/** @brief 正式来源结束后把必要结果归入依赖方，再解除身份引用。 */
void dominoHumanResolveTaskReferences(DominoHumanData* data, const DominoTask* source, bool succeeded) {
    for (uint8_t i = 0; i < data->task_count; ++i) {
        DominoTask* target = dominoGetTaskByID(data->task_id_list[i]);
        for (uint8_t d = 0; d < target->dependency_count; ++d) {
            DominoTaskDependency* dependency = &target->dependencies[d];
            if (dependency->task_id != source->task_id || dependency->resolved_result)
                continue;
            dependency->resolved_result = succeeded && dependency->condition == DOMINO_DEPENDENCY_SUCCEEDED ? 1 : 2;
            dependency->task_id = 0;
            dominoHumanStoreAdvanceArrangement(target);
        }
    }
}
void dominoHumanCheckTasks(DominoHuman* human, domino_runtime_ms_t now, bool energy_replaced) {
    DominoHumanData* data = human->data;
    uint32_t order[DOMINO_HUMAN_TASK_COUNT_MAX];
    uint8_t marks[DOMINO_HUMAN_TASK_COUNT_MAX] = {0}, count = 0;
    for (uint8_t i = 0; i < data->task_count; ++i)
        orderTask(data, i, marks, order, &count);
    // 先归并全部本边界行为事实，再按依赖顺序作任务决策。
    for (uint8_t i = 0; i < count; ++i)
        dominoTaskCheckBehavior(dominoGetTaskByID(order[i]), now);
    for (uint8_t i = 0; i < count; ++i) {
        DominoTask* task = dominoGetTaskByID(order[i]);
        if (!task || (task->has_execution && task->execution.status != DOMINO_TASK_STATUS_OPEN))
            continue;
        bool ready = dominoTaskDependenciesReady(task, task->has_execution ? DOMINO_DEPENDENCY_CONTINUE : DOMINO_DEPENDENCY_START);
        DominoTaskDecision decision =
            dominoTaskDecide(task, (DominoTaskDecisionInput){now, data->energy, data->sleep_allowed, ready,
                                                             dominoTaskDependenciesReady(task, DOMINO_DEPENDENCY_COMPLETE), energy_replaced});
        DominoTaskSummary summary = dominoTaskQuery(task, data, now);
        if (decision.action == DOMINO_TASK_SUCCEED && !summary.success_proof)
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "task lacks success proof");
        dominoTaskApplyDecision(task, decision, now);
        if (decision.action == DOMINO_TASK_REVOKE || (task->has_execution && task->execution.status != DOMINO_TASK_STATUS_OPEN)) {
            uint8_t channel = task->channel_index;
            if (data->active_task_id_list[channel] == task->task_id) {
                data->active_task_id_list[channel] = 0;
                bumpQueue(human, channel);
            }
            dominoHumanQueueRemove(human, task->task_id, channel);
            dominoHumanResolveTaskReferences(data, task, decision.action == DOMINO_TASK_SUCCEED);
            if (decision.action == DOMINO_TASK_REVOKE) {
                for (uint8_t j = 0; j < data->task_count; ++j)
                    if (data->task_id_list[j] == task->task_id) {
                        dominoHumanStoreRecycleTask(data, j);
                        break;
                    }
            }
        }
    }
    dominoHumanRefreshStatus(human, now);
}
static void earlier(DominoHumanCheckTime value, uint32_t base, uint32_t* end) {
    if (value.valid && value.at_ms > base && value.at_ms < *end)
        *end = value.at_ms;
}
__attribute__((hot)) void dominoHumanSettle(DominoHuman* human, domino_runtime_ms_t now) {
    DominoHumanData* data = human->data;
    dominoHumanTimeSpan(data->settled_at_ms, now);
    dominoHumanCheckTasks(human, data->settled_at_ms, false);
    while (data->settled_at_ms < now) {
        uint32_t begin = data->settled_at_ms, end = now;
        uint32_t natural_delta = DOMINO_HUMAN_NEED_MINUTE_MS - data->needs_phase_ms;
        bool active = false;
        for (uint8_t i = 0; i < data->task_count; ++i) {
            const DominoTask* task = dominoGetTaskByID(data->task_id_list[i]);
            if (task->has_execution && task->execution.status != DOMINO_TASK_STATUS_OPEN)
                continue;
            DominoTaskSummary summary = dominoTaskQuery(task, data, begin);
            active |= summary.running;
            earlier(summary.next, begin, &end);
            if (!task->has_execution)
                earlier(task->start_deadline, begin, &end);
            earlier(task->finish_deadline, begin, &end);
        }
        // 只有运行效果需要与自然变化逐边界合成；纯等待区间直接使用同一基础计算累计。
        if (active && natural_delta < end - begin)
            end = begin + natural_delta;
        dominoHumanWriteNeeds(data, dominoHumanCalculateNeeds(dominoHumanReadNeeds(data), begin, end));
        for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c) {
            if (data->active_task_id_list[c])
                dominoTaskSettleEffects(dominoGetTaskByID(data->active_task_id_list[c]), data, begin, end);
        }
        dominoHumanCheckTasks(human, end, false);
        data->settled_at_ms = end;
    }
}
void dominoHumanStartReady(DominoHuman* human, domino_runtime_ms_t now) {
    DominoHumanData* data = human->data;
    for (uint8_t pass = 0; pass < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++pass) {
        bool started = false;
        for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c) {
            uint32_t id = data->active_task_id_list[c];
            bool acquiring = id == 0;
            if (!id && data->queue_count[c])
                id = data->channel_queue[c][0];
            if (!id)
                continue;
            DominoTask* task = dominoGetTaskByID(id);
            DominoTaskSummary summary = dominoTaskQuery(task, data, now);
            if (!summary.can_start)
                continue;
            bool conflict = false;
            for (uint8_t other = 0; other < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++other) {
                if (other == c || !data->active_task_id_list[other])
                    continue;
                DominoTaskSummary occupied = dominoTaskQuery(dominoGetTaskByID(data->active_task_id_list[other]), data, now);
                if ((acquiring && occupied.task_key == summary.task_key) ||
                    (occupied.holds_behavior && occupied.behavior_key == summary.behavior_key))
                    conflict = true;
            }
            if (conflict || kv_size(domino_all_behavior_list) >= DOMINO_BEHAVIOR_POOL_MAX) {
                if (!task->retry.valid || task->retry.at_ms <= now)
                    task->retry = (DominoHumanCheckTime){dominoHumanTimeAdd(now, 3000), true};
                continue;
            }
            task->retry.valid = false;
            dominoTaskStartBehavior(task, now);
            if (acquiring) {
                dominoHumanQueueRemove(human, id, c);
                data->active_task_id_list[c] = id;
                bumpQueue(human, c);
            }
            started = true;
        }
        if (!started)
            break;
    }
    dominoHumanRefreshStatus(human, now);
}
void dominoHumanRefreshStatus(DominoHuman* human, domino_runtime_ms_t now) {
    const DominoHumanData* data = human->data;
    human->status = DOMINO_HUMAN_STATUS_THINKING;
    for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c) {
        uint32_t id = data->active_task_id_list[c];
        bool acquiring = id == 0;
        if (!id && data->queue_count[c])
            id = data->channel_queue[c][0];
        if (!id)
            continue;
        DominoTaskSummary summary = dominoTaskQuery(dominoGetTaskByID(id), data, now);
        if (summary.running) {
            human->status = DOMINO_HUMAN_STATUS_NORMAL;
            return;
        }
        if (!summary.can_start || kv_size(domino_all_behavior_list) >= DOMINO_BEHAVIOR_POOL_MAX)
            continue;
        bool conflict = false;
        for (uint8_t other = 0; other < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++other) {
            if (other == c || !data->active_task_id_list[other])
                continue;
            DominoTaskSummary occupied = dominoTaskQuery(dominoGetTaskByID(data->active_task_id_list[other]), data, now);
            conflict |=
                (acquiring && occupied.task_key == summary.task_key) || (occupied.holds_behavior && occupied.behavior_key == summary.behavior_key);
        }
        if (!conflict) {
            human->status = DOMINO_HUMAN_STATUS_NORMAL;
            return;
        }
    }
}
void dominoHumanRecycle(DominoHuman* human) {
    for (uint8_t i = 0; i < human->data->task_count;) {
        DominoTask* task = dominoGetTaskByID(human->data->task_id_list[i]);
        dominoHumanStoreRecycleBehaviors(task);
        if (task->has_execution && task->execution.status != DOMINO_TASK_STATUS_OPEN) {
            dominoHumanResolveTaskReferences(human->data, task, task->execution.status == DOMINO_TASK_STATUS_SUCCEEDED);
            dominoHumanStoreRecycleTask(human->data, i);
        } else
            ++i;
    }
}
/** @brief 预测只保存会变化的局部事实；实体指针仅在调用方同一次读锁内借用。 */
typedef struct ProjectedTask {
    const DominoTask* task;
    uint64_t successes;
    domino_behavior_id_t current_id;
    DOMINO_TASK_STATUS status;
    int8_t channel;
} ProjectedTask;
static bool projectedDependencies(const DominoTask* task, uint8_t stage, const ProjectedTask* projected, uint8_t count,
                                  const DominoBehavior* behaviors) {
    for (uint8_t d = 0; d < task->dependency_count; ++d) {
        const DominoTaskDependency* dep = &task->dependencies[d];
        if (!(dep->stages & stage))
            continue;
        if (dep->resolved_result) {
            if (dep->resolved_result == 1)
                continue;
            return false;
        }
        const ProjectedTask* source = nullptr;
        for (uint8_t i = 0; i < count; ++i)
            if (projected[i].task->task_id == dep->task_id) {
                source = &projected[i];
                break;
            }
        if (!source)
            return false;
        if (dep->condition == DOMINO_DEPENDENCY_SUCCEEDED) {
            if (source->status != DOMINO_TASK_STATUS_SUCCEEDED)
                return false;
        } else if (source->status != DOMINO_TASK_STATUS_OPEN || !source->current_id || source->channel < 0 ||
                   behaviors[source->channel].status != DOMINO_BEHAVIOR_STATUS_ACTIVE)
            return false;
    }
    return true;
}
static void projectBoundary(ProjectedTask* projected, uint8_t count, DominoBehavior* behaviors, DominoNeedValues needs, bool sleep_allowed,
                            uint32_t now) {
    for (uint8_t i = 0; i < count; ++i) {
        ProjectedTask* value = &projected[i];
        if (!value->current_id || value->channel < 0)
            continue;
        DominoBehavior* behavior = &behaviors[value->channel];
        DOMINO_BEHAVIOR_STATUS status = dominoBehaviorDecide(behavior);
        if (status >= DOMINO_BEHAVIOR_STATUS_SUCCEEDED) {
            if (status == DOMINO_BEHAVIOR_STATUS_SUCCEEDED) {
                if (value->successes == UINT64_MAX)
                    DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "projected proof overflow");
                ++value->successes;
            }
            behavior->status = status;
            value->current_id = 0;
        }
    }
    // 数组已按依赖拓扑顺序排列；截止边界也先确定来源结果。
    for (uint8_t i = 0; i < count; ++i) {
        ProjectedTask* value = &projected[i];
        if (value->status != DOMINO_TASK_STATUS_OPEN)
            continue;
        DominoTask local = *value->task;
        local.execution.successful_behavior_count = value->successes;
        local.execution.current_behavior_id = value->current_id;
        DominoTaskDecision decision = dominoTaskDecide(
            &local,
            (DominoTaskDecisionInput){now, needs.energy, sleep_allowed,
                                      projectedDependencies(&local, local.has_execution ? DOMINO_DEPENDENCY_CONTINUE : DOMINO_DEPENDENCY_START,
                                                            projected, count, behaviors),
                                      projectedDependencies(&local, DOMINO_DEPENDENCY_COMPLETE, projected, count, behaviors), false});
        if (decision.cancel_behavior && value->current_id) {
            behaviors[value->channel].status = DOMINO_BEHAVIOR_STATUS_CANCELLED;
            value->current_id = 0;
        }
        if (decision.action == DOMINO_TASK_SUCCEED)
            value->status = DOMINO_TASK_STATUS_SUCCEEDED;
        else if (decision.action == DOMINO_TASK_FAIL)
            value->status = DOMINO_TASK_STATUS_FAILED;
        else if (decision.action == DOMINO_TASK_CANCEL || decision.action == DOMINO_TASK_REVOKE)
            value->status = DOMINO_TASK_STATUS_CANCELLED;
    }
}
bool dominoHumanPredictNeeds(const DominoHuman* human, domino_runtime_ms_t now, uint32_t* budget, DominoNeedValues* result) {
    const DominoHumanData* data = human->data;
    uint32_t base = data->settled_at_ms;
    dominoHumanTimeSpan(base, now);
    DominoNeedValues needs = dominoHumanReadNeeds(data);
    DominoBehavior behaviors[DOMINO_HUMAN_TASK_CHANNEL_COUNT] = {0};
    ProjectedTask projected[DOMINO_HUMAN_TASK_COUNT_MAX];
    uint32_t order[DOMINO_HUMAN_TASK_COUNT_MAX];
    uint8_t marks[DOMINO_HUMAN_TASK_COUNT_MAX] = {0}, count = 0;
    for (uint8_t i = 0; i < data->task_count; ++i)
        orderTask(data, i, marks, order, &count);
    for (uint8_t i = 0; i < count; ++i) {
        const DominoTask* task = dominoGetTaskByID(order[i]);
        projected[i] = (ProjectedTask){task, task->execution.successful_behavior_count, task->execution.current_behavior_id,
                                       task->has_execution ? task->execution.status : DOMINO_TASK_STATUS_OPEN, -1};
        if (task->execution.current_behavior_id) {
            projected[i].channel = (int8_t)task->channel_index;
            behaviors[task->channel_index] = *dominoGetBehaviorByID(task->execution.current_behavior_id);
        }
    }
    for (;;) {
        if (!*budget)
            return false;
        --*budget;
        projectBoundary(projected, count, behaviors, needs, data->sleep_allowed, base);
        if (base == now) {
            *result = needs;
            return true;
        }
        bool active = false;
        uint32_t end = now, delta = DOMINO_HUMAN_NEED_MINUTE_MS - needs.phase_ms;
        if (delta < end - base)
            end = base + delta;
        for (uint8_t i = 0; i < count; ++i) {
            const ProjectedTask* value = &projected[i];
            if (value->status != DOMINO_TASK_STATUS_OPEN)
                continue;
            earlier(value->task->finish_deadline, base, &end);
            if (!value->task->has_execution)
                earlier(value->task->start_deadline, base, &end);
            if (!value->current_id || behaviors[value->channel].status != DOMINO_BEHAVIOR_STATUS_ACTIVE)
                continue;
            active = true;
            delta = dominoBehaviorNextDelta(&behaviors[value->channel]);
            if (delta < end - base)
                end = base + delta;
        }
        if (!active) {
            *result = dominoHumanCalculateNeeds(needs, base, now);
            return true;
        }
        needs = dominoHumanCalculateNeeds(needs, base, end);
        for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c)
            if (behaviors[c].behavior_id && behaviors[c].status == DOMINO_BEHAVIOR_STATUS_ACTIVE)
                dominoBehaviorCalculate(&behaviors[c], &needs, base, end);
        base = end;
    }
}
