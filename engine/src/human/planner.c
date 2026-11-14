#include "planner.h"

#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "../time/entry.h"
#include "candidate.h"
#include "rules.h"
#include "runtime.h"

typedef struct HumanPlanner {
    DominoThreadQueue requests[DOMINO_PLAN_PRIORITY_COUNT];
    DominoThreadQueue results[DOMINO_PLAN_PRIORITY_COUNT];
    DominoThreadQueue* wake_queue;
    thrd_t thread;
    DominoPlannerMetrics metrics;
    DominoPlannerConfig config;
    DominoPlannerKind kind;
} HumanPlanner;
static HumanPlanner g_planners[DOMINO_PLANNER_COUNT];
static void requireCode(DOMINO_CODE code) {
    if (code != CODE_OK)
        DOMINO_EXIT_WITH_ERROR(code, "planner queue operation failed");
}
bool dominoHumanPlannerMatches(const DominoHuman* human, DominoPlannerKind kind, DominoPlanPriority priority, uint64_t id) {
    if (!human || kind >= DOMINO_PLANNER_COUNT || priority >= DOMINO_PLAN_PRIORITY_COUNT || !id)
        return false;
    const DominoPlanRequestState* request = &human->runtime->requests[kind];
    return request->request_id == id && request->request_priority == priority;
}
static void processRequest(HumanPlanner* planner, DominoPlanPriority priority, const DominoHumanPlanRequestMessage* message) {
    uint64_t start = dominoMonotonicTimeNs();
    planner->metrics.queue_ns[priority] += start - message->queued_at_ns;
    dominoTimingRecord(&planner->metrics.queue_samples[priority], start - message->queued_at_ns);
    ++planner->metrics.requests[priority];
    dominoHumanReadLock();
    uint64_t locked = dominoMonotonicTimeNs();
    const DominoHuman* human = dominoGetHumanByID(message->human_id);
    uint32_t now;
    dominoTimeGetRuntimeMs(&now);
    DominoHumanPlanResultMessage result = {
        .request_id = message->request_id, .human_id = message->human_id, .planner_kind = planner->kind, .request_priority = priority};
    bool valid =
        dominoHumanPlannerMatches(human, planner->kind, priority, message->request_id) && now < human->runtime->requests[planner->kind].deadline_ms;
    if (valid) {
        DominoPlanScope scope = human->runtime->requests[planner->kind].trigger_scope;
        switch (planner->kind) {
            case DOMINO_PLANNER_MOTIVATION:
                result.candidate = dominoHumanPlanMotivations(human, scope, now, planner->config.computation_budget, &result.outcome);
                break;
            case DOMINO_PLANNER_TASK:
                result.candidate = dominoHumanPlanTasks(human, scope, now, planner->config.computation_budget, &result.outcome);
                break;
            case DOMINO_PLANNER_BEHAVIOR:
                result.candidate = dominoHumanPlanBehaviors(human, scope, now, planner->config.computation_budget, &result.outcome);
                break;
            default:
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "planner kind invalid");
        }
    } else
        ++planner->metrics.stale_count;
    uint64_t lock_ns = dominoMonotonicTimeNs() - locked;
    planner->metrics.read_lock_ns += lock_ns;
    dominoTimingRecord(&planner->metrics.lock_samples, lock_ns);
    dominoHumanReadUnlock();
    if (valid) {
        result.queued_at_ns = dominoMonotonicTimeNs();
        requireCode(dominoThreadQueueProduce(&planner->results[priority], &result, false));
        dominoThreadQueueWakeAll(planner->wake_queue);
    }
    uint64_t elapsed = dominoMonotonicTimeNs() - start;
    planner->metrics.request_ns[priority] += elapsed;
    dominoTimingRecord(&planner->metrics.request_samples[priority], elapsed);
}
static bool consumePlannerBatch(HumanPlanner* planner) {
    uint64_t start = dominoMonotonicTimeNs();
    uint32_t taken[DOMINO_PLAN_PRIORITY_COUNT] = {0};
    for (uint8_t p = 0; p < DOMINO_PLAN_PRIORITY_COUNT; ++p) {
        uint32_t quota = p == DOMINO_PLAN_HIGH ? planner->config.high_quota : 1;
        for (; taken[p] < quota; ++taken[p]) {
            DominoHumanPlanRequestMessage message;
            DOMINO_CODE code = dominoThreadQueueConsume(&planner->requests[p], &message, false);
            if (code == ERR_QUEUE_EMPTY)
                break;
            requireCode(code);
            processRequest(planner, (DominoPlanPriority)p, &message);
        }
    }
    if (taken[0] || taken[1]) {
        ++planner->metrics.batch_count;
        uint64_t elapsed = dominoMonotonicTimeNs() - start;
        planner->metrics.batch_ns += elapsed;
        dominoTimingRecord(&planner->metrics.batch_samples, elapsed);
        if (taken[0] > planner->metrics.max_high_batch)
            planner->metrics.max_high_batch = taken[0];
        if (taken[1] > planner->metrics.max_normal_batch)
            planner->metrics.max_normal_batch = taken[1];
        return true;
    }
    return false;
}
/** @brief 唯一私有循环：固定 K 高 + 1 普通；失效项仍占额度，计算不跨批次。 */
static int plannerThread(void* pointer) {
    HumanPlanner* planner = pointer;
    for (;;) {
        uint64_t epoch;
        requireCode(dominoThreadQueueSnapshot(&planner->requests[DOMINO_PLAN_NORMAL], &epoch));
        if (dominoEngineIsStopping())
            return 0;
        uint64_t freeze_epoch = dominoTimeModuleGetFreezeEpoch();
        if (freeze_epoch) {
            dominoTimeModuleWait(freeze_epoch);
            continue;
        }
        if (consumePlannerBatch(planner))
            continue;
        requireCode(dominoThreadQueueWait(&planner->requests[DOMINO_PLAN_NORMAL], epoch));
    }
}
void dominoHumanPlannerInit(DominoThreadQueue* wake_queue) {
    // 十二队列最大槽位约 48 MiB；候选实际按需分配，额外预算见 candidate.c。
    static_assert(6ULL * DOMINO_THREAD_QUEUE_MAX_CAPACITY * (sizeof(DominoHumanPlanRequestMessage) + sizeof(DominoHumanPlanResultMessage)) <=
                      64ULL * 1024 * 1024,
                  "planning queue budget");
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k) {
        HumanPlanner* planner = &g_planners[k];
        planner->kind = (DominoPlannerKind)k;
        planner->wake_queue = wake_queue;
        planner->config = g_domino_engine_launch_config.planners[k];
        if (!planner->config.high_quota && !planner->config.request_timeout_ms && !planner->config.computation_budget)
            planner->config = (DominoPlannerConfig){4, 3000, 512};
        if (!planner->config.high_quota || planner->config.high_quota > 63 || !planner->config.request_timeout_ms ||
            !planner->config.computation_budget || planner->config.computation_budget > 4096)
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "invalid planner quota, timeout or budget");
        for (uint8_t p = 0; p < DOMINO_PLAN_PRIORITY_COUNT; ++p) {
            dominoThreadQueueInit(&planner->requests[p], sizeof(DominoHumanPlanRequestMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                                  DOMINO_THREAD_QUEUE_MAX_CAPACITY);
            dominoThreadQueueInit(&planner->results[p], sizeof(DominoHumanPlanResultMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                                  DOMINO_THREAD_QUEUE_MAX_CAPACITY);
        }
    }
}
void dominoHumanPlannerStart(void) {
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k)
        DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_planners[k].thread, plannerThread, &g_planners[k]), ERR_THREAD_CREATE, "cannot start planner");
}
void dominoHumanPlannerStop(void) {
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k)
        DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_planners[k].thread, nullptr), ERR_THREAD_JOIN, "cannot join planner");
}
void dominoHumanPlannerNotifyTimeChanged(void) {
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k)
        dominoThreadQueueWakeAll(&g_planners[k].requests[DOMINO_PLAN_NORMAL]);
}
void dominoHumanPlannerSubmit(DominoHuman* human, DominoPlannerKind kind, DominoPlanPriority priority, DominoPlanScope scope) {
    HumanPlanner* planner = &g_planners[kind];
    uint32_t now;
    dominoTimeGetRuntimeMs(&now);
    uint32_t deadline = dominoHumanTimeAdd(now, planner->config.request_timeout_ms);
    DominoHumanPlanRequestMessage message = {.human_id = human->human_id, .queued_at_ns = dominoMonotonicTimeNs()};
    requireCode(dominoThreadQueueProduce(&planner->requests[priority], &message, false));
    human->runtime->requests[kind] = (DominoPlanRequestState){message.request_id, scope, deadline, priority};
    if (priority == DOMINO_PLAN_HIGH)
        dominoThreadQueueWakeAll(&planner->requests[DOMINO_PLAN_NORMAL]);
}
bool dominoHumanPlannerConsume(DominoPlannerKind kind, DominoPlanPriority priority, DominoHumanPlanResultMessage* result) {
    DOMINO_CODE code = dominoThreadQueueConsume(&g_planners[kind].results[priority], result, false);
    if (code == ERR_QUEUE_EMPTY)
        return false;
    requireCode(code);
    return true;
}
DominoPlannerMetrics dominoHumanPlannerMetrics(DominoPlannerKind kind) {
    return g_planners[kind].metrics;
}
void dominoHumanPlannerDestroy(void) {
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k)
        for (uint8_t p = 0; p < DOMINO_PLAN_PRIORITY_COUNT; ++p) {
            DominoHumanPlanResultMessage result;
            while (dominoHumanPlannerConsume((DominoPlannerKind)k, (DominoPlanPriority)p, &result))
                dominoHumanFreeCandidate(result.candidate);
            dominoThreadQueueDestroy(&g_planners[k].requests[p]);
            dominoThreadQueueDestroy(&g_planners[k].results[p]);
        }
    dominoHumanCandidateResetMetrics();
    memset(g_planners, 0, sizeof(g_planners));
}

#ifdef DOMINO_PLAYGROUND_MODE
#include "playground.h"
void dominoHumanPlannerTestLeaveCandidate(DominoHuman* human) {
    dominoHumanPlannerSubmit(human, DOMINO_PLANNER_MOTIVATION, DOMINO_PLAN_HIGH, (DominoPlanScope){0});
    if (!consumePlannerBatch(&g_planners[DOMINO_PLANNER_MOTIVATION]) || !dominoHumanCandidateLiveBytes())
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "queued candidate fixture missing");
}
void dominoHumanPlannerTestFairness(void) {
    for (uint8_t k = 0; k < DOMINO_PLANNER_COUNT; ++k) {
        HumanPlanner* planner = &g_planners[k];
        uint32_t quota = planner->config.high_quota;
        uint64_t epoch;
        requireCode(dominoThreadQueueSnapshot(&planner->requests[DOMINO_PLAN_NORMAL], &epoch));
        for (uint32_t i = 0; i < quota * 2; ++i) {
            DominoHumanPlanRequestMessage request = {.human_id = UINT32_MAX, .queued_at_ns = dominoMonotonicTimeNs()};
            requireCode(dominoThreadQueueProduce(&planner->requests[DOMINO_PLAN_HIGH], &request, false));
        }
        dominoThreadQueueWakeAll(&planner->requests[DOMINO_PLAN_NORMAL]);
        // 检查与 Wait 之间仅高队列到达，也必须由统一等待对象的版本发现。
        requireCode(dominoThreadQueueWait(&planner->requests[DOMINO_PLAN_NORMAL], epoch));
        for (uint32_t i = 0; i < 2; ++i) {
            DominoHumanPlanRequestMessage request = {.human_id = UINT32_MAX, .queued_at_ns = dominoMonotonicTimeNs()};
            requireCode(dominoThreadQueueProduce(&planner->requests[DOMINO_PLAN_NORMAL], &request, false));
        }
        if (!consumePlannerBatch(planner) || planner->metrics.requests[0] != quota || planner->metrics.requests[1] != 1 ||
            planner->metrics.stale_count != quota + 1)
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "fixed planner fairness failed");
        if (!consumePlannerBatch(planner) || planner->metrics.requests[0] != quota * 2 || planner->metrics.requests[1] != 2 ||
            consumePlannerBatch(planner))
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "planner FIFO quota failed");
    }
}
#endif
