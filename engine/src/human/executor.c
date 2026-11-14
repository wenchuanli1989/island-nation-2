#include "executor.h"

#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "../game_timer/entry.h"
#include "../time/entry.h"
#include "advance.h"
#include "behavior/entry.h"
#include "candidate.h"
#include "data.h"
#include "motivation/entry.h"
#include "planner.h"
#include "rules.h"
#include "runtime.h"
#include "state.h"
#include "store.h"
#include "task/entry.h"

typedef struct HumanExecution {
    DominoExecutionMetrics metrics;
    DominoThreadQueue inbox;
    thrd_t thread;
    struct DominoHumanRuntime* runtimes;
    domino_human_id_t pending[DOMINO_HUMAN_WORLD_COUNT_MAX];
    uint32_t pending_head, pending_count;
    uint64_t batch_id;
} HumanExecution;
static HumanExecution g_execution;
enum { RESULT_QUOTA = 8, BUSINESS_QUOTA = 16, LOCAL_QUOTA = 16 };
static_assert(RESULT_QUOTA * 6 + BUSINESS_QUOTA == 64, "all seven queues have a positive bounded quota");
DominoExecutionMetrics dominoHumanExecutionMetrics(void) {
    return g_execution.metrics;
}
static void requireCode(DOMINO_CODE code) {
    if (code != CODE_OK)
        DOMINO_EXIT_WITH_ERROR(code, "human execution queue operation failed");
}
static void markPending(DominoHuman* human) {
    if (human->runtime->pending)
        return;
    if (g_execution.pending_count == DOMINO_HUMAN_WORLD_COUNT_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "human pending capacity");
    uint32_t index = (g_execution.pending_head + g_execution.pending_count) % DOMINO_HUMAN_WORLD_COUNT_MAX;
    g_execution.pending[index] = human->human_id;
    ++g_execution.pending_count;
    human->runtime->pending = true;
}
static void cancelWakeup(DominoHuman* human) {
    uint64_t id = human->wakeup_game_timer_id;
    human->wakeup_game_timer_id = 0;
    human->wakeup_due_ms = 0;
    if (!id)
        return;
    DOMINO_CODE code = dominoGameTimerCancel(id, &g_execution.inbox);
    if (code != CODE_OK && code != ERR_NOT_FOUND)
        requireCode(code);
}
static void scheduleWakeup(DominoHuman* human, DominoHumanCheckTime time) {
    if (!time.valid) {
        cancelWakeup(human);
        return;
    }
    if (human->wakeup_game_timer_id && human->wakeup_due_ms <= time.at_ms)
        return;
    cancelWakeup(human);
    requireCode(dominoGameTimerScheduleAt(&g_execution.inbox, time.at_ms, 1, human->human_id, &human->wakeup_game_timer_id));
    human->wakeup_due_ms = time.at_ms;
}
static bool scopeNeeded(const DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope, uint32_t now) {
    uint32_t id = scope.object_id;
    const DominoHumanData* data = human->data;
    if (kind == DOMINO_PLANNER_MOTIVATION) {
        if (!id)
            return !data->motivation_count;
        const DominoMotivation* motivation = dominoGetMotivationByID(id);
        return motivation && motivation->owner_human_id == human->human_id && dominoMotivationNeedsReassessment(motivation, data);
    }
    if (kind == DOMINO_PLANNER_TASK && scope.accepted_task) {
        const DominoTask* task = dominoGetTaskByID(id);
        return task && task->owner_human_id == human->human_id && dominoTaskQuery(task, data, now).needs_arrangement;
    }
    if (kind == DOMINO_PLANNER_TASK) {
        const DominoMotivation* motivation = dominoGetMotivationByID(id);
        return motivation && motivation->owner_human_id == human->human_id && dominoMotivationNeedsTask(motivation, data);
    }
    const DominoTask* task = dominoGetTaskByID(id);
    return task && task->owner_human_id == human->human_id && dominoTaskQuery(task, data, now).needs_behavior;
}
static DominoHumanCheckTime* retryFor(DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope) {
    uint32_t id = scope.object_id;
    if (kind == DOMINO_PLANNER_MOTIVATION)
        return &human->data->motivation_retry;
    if (kind == DOMINO_PLANNER_TASK && !scope.accepted_task) {
        DominoMotivation* motivation = dominoGetMotivationByID(id);
        return motivation ? &motivation->retry : nullptr;
    }
    DominoTask* task = dominoGetTaskByID(id);
    return task ? &task->retry : nullptr;
}
static void retryScope(DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope, uint32_t now) {
    DominoHumanCheckTime* retry = retryFor(human, kind, scope);
    if (retry)
        *retry = (DominoHumanCheckTime){dominoHumanTimeAdd(now, 3000), true};
}
static bool eligible(DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope, uint32_t now) {
    if (!scopeNeeded(human, kind, scope, now))
        return false;
    DominoHumanCheckTime* retry = retryFor(human, kind, scope);
    if (retry && retry->valid) {
        if (now < retry->at_ms)
            return false;
        retry->valid = false;
    }
    return true;
}
static void offerOpportunity(DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope, uint32_t now) {
    struct DominoHumanRuntime* runtime = human->runtime;
    if (!eligible(human, kind, scope, now))
        return;
    if (runtime->requests[kind].request_id && runtime->requests[kind].trigger_scope.object_id == scope.object_id &&
        runtime->requests[kind].trigger_scope.accepted_task == scope.accepted_task)
        return;
    for (uint8_t i = 0; i < runtime->opportunity_count[kind]; ++i)
        if (runtime->opportunities[kind][i].object_id == scope.object_id && runtime->opportunities[kind][i].accepted_task == scope.accepted_task)
            return;
    if (runtime->opportunity_count[kind] == DOMINO_PLAN_OPPORTUNITY_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "planning opportunity capacity");
    runtime->opportunities[kind][runtime->opportunity_count[kind]++] = scope;
}
static void refreshOpportunities(DominoHuman* human, uint32_t now) {
    struct DominoHumanRuntime* runtime = human->runtime;
    if (human->data->motivation_retry.valid && human->data->motivation_retry.at_ms <= now)
        human->data->motivation_retry.valid = false;
    for (uint8_t i = 0; i < human->data->motivation_count; ++i) {
        DominoMotivation* motivation = dominoGetMotivationByID(human->data->motivation_ids[i]);
        if (motivation->retry.valid && motivation->retry.at_ms <= now)
            motivation->retry.valid = false;
    }
    for (uint8_t i = 0; i < human->data->task_count; ++i) {
        DominoTask* task = dominoGetTaskByID(human->data->task_id_list[i]);
        if (task->retry.valid && task->retry.at_ms <= now)
            task->retry.valid = false;
    }
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k) {
        DominoPlanRequestState* request = &runtime->requests[k];
        if (request->request_id && (now >= request->deadline_ms || !scopeNeeded(human, (DominoPlannerKind)k, request->trigger_scope, now))) {
            if (now >= request->deadline_ms)
                retryScope(human, (DominoPlannerKind)k, request->trigger_scope, now);
            request->request_id = 0;
        }
        uint8_t retained = 0;
        for (uint8_t i = 0; i < runtime->opportunity_count[k]; ++i) {
            DominoPlanScope scope = runtime->opportunities[k][i];
            if (eligible(human, (DominoPlannerKind)k, scope, now))
                runtime->opportunities[k][retained++] = scope;
        }
        runtime->opportunity_count[k] = retained;
    }
    if (!human->data->motivation_count)
        offerOpportunity(human, DOMINO_PLANNER_MOTIVATION, (DominoPlanScope){.object_id = 0}, now);
    for (uint8_t i = 0; i < human->data->motivation_count; ++i) {
        uint32_t id = human->data->motivation_ids[i];
        offerOpportunity(human, DOMINO_PLANNER_MOTIVATION, (DominoPlanScope){.object_id = id}, now);
        offerOpportunity(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = id}, now);
    }
    // 初始机会按保存的通道逻辑顺序恢复；已有机会次序不被重复扫描刷新。
    for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c) {
        uint32_t id = human->data->active_task_id_list[c];
        if (id) {
            offerOpportunity(human, DOMINO_PLANNER_TASK, (DominoPlanScope){id, true}, now);
            offerOpportunity(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = id}, now);
        }
        for (uint8_t q = 0; q < human->data->queue_count[c]; ++q) {
            id = human->data->channel_queue[c][q];
            offerOpportunity(human, DOMINO_PLANNER_TASK, (DominoPlanScope){id, true}, now);
            offerOpportunity(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = id}, now);
        }
    }
}
static void includeTime(DominoHumanCheckTime* best, DominoHumanCheckTime value, uint32_t now) {
    if (value.valid && value.at_ms > now && (!best->valid || value.at_ms < best->at_ms))
        *best = value;
}
static void maintainWakeup(DominoHuman* human, uint32_t now) {
    DominoHumanData* data = human->data;
    DominoHumanCheckTime best = {0};
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k) {
        DominoPlanRequestState* request = &human->runtime->requests[k];
        if (request->request_id)
            includeTime(&best, (DominoHumanCheckTime){request->deadline_ms, true}, now);
        else if (human->runtime->opportunity_count[k])
            markPending(human);
    }
    includeTime(&best, data->motivation_retry, now);
    for (uint8_t i = 0; i < data->motivation_count; ++i)
        includeTime(&best, dominoGetMotivationByID(data->motivation_ids[i])->retry, now);
    for (uint8_t i = 0; i < data->task_count; ++i) {
        const DominoTask* task = dominoGetTaskByID(data->task_id_list[i]);
        if (task->has_execution && task->execution.status != DOMINO_TASK_STATUS_OPEN)
            continue;
        DominoTaskSummary summary = dominoTaskQuery(task, data, now);
        includeTime(&best, summary.next, now);
        includeTime(&best, task->retry, now);
        includeTime(&best, task->finish_deadline, now);
        if (!task->has_execution)
            includeTime(&best, task->start_deadline, now);
        if (data->queue_count[task->channel_index] && data->channel_queue[task->channel_index][0] == task->task_id)
            includeTime(&best, task->not_before, now);
    }
    if (data->energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        uint32_t delta = (data->energy - DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) * DOMINO_HUMAN_NEED_MINUTE_MS - data->needs_phase_ms;
        includeTime(&best, (DominoHumanCheckTime){dominoHumanTimeAdd(now, delta), true}, now);
    }
    scheduleWakeup(human, best);
}
static void advanceHuman(DominoHuman* human, uint32_t now) {
    dominoHumanSettle(human, now);
    dominoHumanRecycle(human);
    dominoHumanStartReady(human, now);
    refreshOpportunities(human, now);
}
static void processPending(DominoHuman* human) {
    uint32_t now;
    dominoTimeGetRuntimeMs(&now);
    advanceHuman(human, now);
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k) {
        struct DominoHumanRuntime* runtime = human->runtime;
        if (runtime->requests[k].request_id || !runtime->opportunity_count[k] || runtime->posted_batch[k] == g_execution.batch_id)
            continue;
        DominoPlanScope scope = runtime->opportunities[k][0];
        --runtime->opportunity_count[k];
        memmove(runtime->opportunities[k], runtime->opportunities[k] + 1, runtime->opportunity_count[k] * sizeof(DominoPlanScope));
        dominoHumanPlannerSubmit(human, (DominoPlannerKind)k, dominoHumanPlanningPriority(human, (DominoPlannerKind)k, scope), scope);
        runtime->posted_batch[k] = g_execution.batch_id;
    }
    maintainWakeup(human, now);
}
static void processResult(DominoHumanPlanResultMessage* result) {
    ++g_execution.metrics.results;
    if (result->queued_at_ns)
        dominoTimingRecord(&g_execution.metrics.result_wait[result->planner_kind][result->request_priority],
                           dominoMonotonicTimeNs() - result->queued_at_ns);
    dominoRWLockWriteLock(&domino_human_data_lock);
    uint64_t lock_start = dominoMonotonicTimeNs();
    DominoHuman* human = dominoGetHumanByID(result->human_id);
    if (dominoHumanPlannerMatches(human, result->planner_kind, result->request_priority, result->request_id)) {
        uint32_t now;
        dominoTimeGetRuntimeMs(&now);
        DominoPlanRequestState* request = &human->runtime->requests[result->planner_kind];
        DominoPlanScope scope = request->trigger_scope;
        bool expired = now >= request->deadline_ms;
        dominoHumanSettle(human, now);
        bool needed = scopeNeeded(human, result->planner_kind, scope, now);
        bool accepted = !expired && needed && result->outcome == DOMINO_PLAN_CANDIDATE &&
                        dominoHumanAcceptCandidate(human, result->planner_kind, scope, result->candidate, now);
        if (!accepted)
            ++g_execution.metrics.rejected;
        request->request_id = 0;
        dominoHumanCheckTasks(human, now, false);
        if (needed && (!accepted || scopeNeeded(human, result->planner_kind, scope, now)))
            retryScope(human, result->planner_kind, scope, now);
        markPending(human);
    } else
        ++g_execution.metrics.stale;
    dominoTimingRecord(&g_execution.metrics.write_lock, dominoMonotonicTimeNs() - lock_start);
    dominoRWLockWriteUnlock(&domino_human_data_lock);
    dominoHumanFreeCandidate(result->candidate);
}
static void processMessage(const DominoHumanExecutionMessage* message) {
    DominoEngineMessage response = {.type = DOMINO_ENGINE_MSG_NPC_RESULT};
    bool reply = false;
    dominoRWLockWriteLock(&domino_human_data_lock);
    uint64_t lock_start = dominoMonotonicTimeNs();
    DominoHuman* human = nullptr;
    if (message->type == DOMINO_HUMAN_EXECUTION_MESSAGE_NAME) {
        dominoHumanSetDisplayName(message->payload.name.client_id, &message->payload.name.request, &response.payload.result);
        reply = true;
    } else if (message->type == DOMINO_HUMAN_EXECUTION_MESSAGE_GAME_TIMER) {
        const DominoGameTimerRequestMessage* timer = &message->payload.game_timer;
        if (!timer->cancelled_flag) {
            human = dominoGetHumanByID((uint32_t)timer->data_id);
            if (human && human->wakeup_game_timer_id == timer->game_timer_id) {
                human->wakeup_game_timer_id = 0;
                human->wakeup_due_ms = 0;
            }
        }
    } else {
        uint32_t id =
            message->type == DOMINO_HUMAN_EXECUTION_MESSAGE_SLEEP_CONDITION ? message->payload.condition.human_id : message->payload.energy.human_id;
        human = dominoGetHumanByID(id);
        if (human) {
            // 精力随时间变化，必须先结算旧区间再接受绝对值；纯条件的同值通知可直接忽略。
            bool changed = message->type != DOMINO_HUMAN_EXECUTION_MESSAGE_SLEEP_CONDITION ||
                           human->data->sleep_allowed != message->payload.condition.sleep_allowed;
            if (changed) {
                uint32_t now;
                dominoTimeGetRuntimeMs(&now);
                dominoHumanSettle(human, now);
                if (message->type == DOMINO_HUMAN_EXECUTION_MESSAGE_SLEEP_CONDITION)
                    dominoHumanApplySleepCondition(human, message->payload.condition.sleep_allowed);
                else
                    dominoHumanApplyEnergy(human, message->payload.energy.energy);
                dominoHumanCheckTasks(human, now, message->type == DOMINO_HUMAN_EXECUTION_MESSAGE_ENERGY);
                refreshOpportunities(human, now);
                maintainWakeup(human, now);
            } else
                human = nullptr;
        }
    }
    if (human)
        markPending(human);
    uint64_t queued = message->type == DOMINO_HUMAN_EXECUTION_MESSAGE_SLEEP_CONDITION ? message->payload.condition.queued_at_ns
                      : message->type == DOMINO_HUMAN_EXECUTION_MESSAGE_ENERGY        ? message->payload.energy.queued_at_ns
                                                                                      : 0;
    if (queued)
        dominoTimingRecord(&g_execution.metrics.condition_latency, dominoMonotonicTimeNs() - queued);
    dominoTimingRecord(&g_execution.metrics.write_lock, dominoMonotonicTimeNs() - lock_start);
    dominoRWLockWriteUnlock(&domino_human_data_lock);
    if (reply)
        dominoLoopProduceToMain(&response);
}
static int executionThread(void* pointer) {
    (void)pointer;
    for (;;) {
        uint64_t epoch;
        requireCode(dominoThreadQueueSnapshot(&g_execution.inbox, &epoch));
        if (dominoEngineIsStopping())
            return 0;
        uint64_t freeze = dominoTimeModuleGetFreezeEpoch();
        if (freeze) {
            dominoTimeModuleWait(freeze);
            continue;
        }
        if (g_execution.batch_id == UINT64_MAX)
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "executor batch counter exhausted");
        ++g_execution.batch_id;
        uint32_t local_count = g_execution.pending_count < LOCAL_QUOTA ? g_execution.pending_count : LOCAL_QUOTA;
        bool worked = false;
        for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k)
            for (uint8_t p = 0; p < DOMINO_PLAN_PRIORITY_COUNT; ++p)
                for (uint8_t i = 0; i < RESULT_QUOTA; ++i) {
                    DominoHumanPlanResultMessage result;
                    if (!dominoHumanPlannerConsume((DominoPlannerKind)k, (DominoPlanPriority)p, &result))
                        break;
                    processResult(&result);
                    worked = true;
                }
        for (uint8_t i = 0; i < BUSINESS_QUOTA; ++i) {
            DominoHumanExecutionMessage message;
            DOMINO_CODE code = dominoThreadQueueConsume(&g_execution.inbox, &message, false);
            if (code == ERR_QUEUE_EMPTY)
                break;
            requireCode(code);
            processMessage(&message);
            worked = true;
        }
        for (uint32_t i = 0; i < local_count; ++i) {
            uint32_t id = g_execution.pending[g_execution.pending_head];
            g_execution.pending_head = (g_execution.pending_head + 1) % DOMINO_HUMAN_WORLD_COUNT_MAX;
            --g_execution.pending_count;
            dominoRWLockWriteLock(&domino_human_data_lock);
            uint64_t lock_start = dominoMonotonicTimeNs();
            DominoHuman* human = dominoGetHumanByID(id);
            human->runtime->pending = false;
            processPending(human);
            dominoTimingRecord(&g_execution.metrics.write_lock, dominoMonotonicTimeNs() - lock_start);
            dominoRWLockWriteUnlock(&domino_human_data_lock);
            worked = true;
        }
        if (worked || g_execution.pending_count)
            continue;
        requireCode(dominoThreadQueueWait(&g_execution.inbox, epoch));
    }
}
void dominoHumanExecutionInit(void) {
    dominoThreadQueueInit(&g_execution.inbox, sizeof(DominoHumanExecutionMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                          DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    dominoHumanPlannerInit(&g_execution.inbox);
}
void dominoHumanExecutionInitAfter(void) {
    size_t count = kv_size(domino_all_human_list);
    if (count > DOMINO_HUMAN_WORLD_COUNT_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "world human limit");
    if (count) {
        g_execution.runtimes = calloc(count, sizeof(*g_execution.runtimes));
        if (!g_execution.runtimes)
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human runtime");
    }
    for (size_t i = 0; i < kv_size(domino_all_task_list); ++i) {
        DominoTask* task = &kv_A(domino_all_task_list, i);
        task->plan_version = 1;
        task->arrangement_version = 1;
        if (!dominoTaskSupported(task->type))
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "unsupported loaded task");
    }
    uint32_t now;
    dominoTimeGetRuntimeMs(&now);
    for (size_t i = 0; i < count; ++i) {
        DominoHuman* human = &kv_A(domino_all_human_list, i);
        human->runtime = &g_execution.runtimes[i];
        for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c)
            human->runtime->channel_version[c] = 1;
        for (uint8_t j = 0; j < human->data->task_count; ++j) {
            const DominoTask* task = dominoGetTaskByID(human->data->task_id_list[j]);
            const DominoBehavior* behavior = dominoGetBehaviorByID(task->execution.current_behavior_id);
            if (behavior && !dominoBehaviorPlanValid(&behavior->plan))
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "unsupported loaded behavior");
            (void)dominoTaskQuery(task, human->data, now);
        }
        dominoHumanRefreshStatus(human, now);
        markPending(human);  // 不结算、不提交规划；Start 后首次有界检查完整工作清单。
        maintainWakeup(human, now);
        if (i % 64 == 63)
            dominoGameTimerPrepareRequests();
    }
    dominoGameTimerPrepareRequests();
}
void dominoHumanExecutionStart(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_execution.thread, executionThread, nullptr), ERR_THREAD_CREATE, "cannot create executor");
    dominoHumanPlannerStart();
}
void dominoHumanExecutionNotifyTimeChanged(void) {
    dominoThreadQueueWakeAll(&g_execution.inbox);
    dominoHumanPlannerNotifyTimeChanged();
}
void dominoHumanExecutionStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_execution.thread, nullptr), ERR_THREAD_JOIN, "cannot join executor");
    dominoHumanPlannerStop();
}
void dominoHumanExecutionSettle(uint32_t now) {
    for (size_t i = 0; i < kv_size(domino_all_human_list); ++i)
        dominoHumanSettle(&kv_A(domino_all_human_list, i), now);
}
void dominoHumanExecutionDestroy(void) {
    dominoHumanPlannerDestroy();
    dominoThreadQueueDestroy(&g_execution.inbox);
    free(g_execution.runtimes);
    memset(&g_execution, 0, sizeof(g_execution));
}
void dominoHumanExecutionPost(DominoHumanExecutionMessage* message) {
    requireCode(dominoThreadQueueProduce(&g_execution.inbox, message, false));
}

#ifdef DOMINO_PLAYGROUND_MODE
#include "playground.h"
void dominoHumanExecutionTestResult(DominoHumanPlanResultMessage* result) {
    processResult(result);
}
#endif
