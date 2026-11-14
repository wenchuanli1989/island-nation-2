#include "playground.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "../entry.h"
#include "../host/entry.h"
#include "../time/entry.h"
#include "advance.h"
#include "behavior/entry.h"
#include "candidate.h"
#include "data.h"
#include "executor.h"
#include "motivation/entry.h"
#include "planner.h"
#include "rules.h"
#include "runtime.h"
#include "state.h"
#include "store.h"
#include "task/entry.h"

#define CHECK(expression)                                                                                            \
    do {                                                                                                             \
        if (!(expression))                                                                                           \
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "AI test failed: %s (%s:%d)", #expression, __FILE__, __LINE__); \
    } while (0)
static struct DominoHumanRuntime g_fixture_runtime;
bool dominoBehaviorFixtureSupported(DOMINO_BEHAVIOR_TYPE type) {
    return type == DOMINO_BEHAVIOR_TYPE_WORK || type == DOMINO_BEHAVIOR_TYPE_PHONE_CALL || type == DOMINO_BEHAVIOR_TYPE_CHAT;
}
void dominoBehaviorFixtureCalculate(DominoBehavior* value, DominoNeedValues* needs, uint32_t begin, uint32_t end) {
    uint32_t span = dominoHumanTimeSpan(begin, end);
    if (value->status != DOMINO_BEHAVIOR_STATUS_ACTIVE)
        return;
    uint32_t remaining = dominoHumanTimeSpan(value->active_elapsed_ms, value->plan.duration_ms);
    if (span > remaining)
        span = remaining;
    uint32_t phase = value->effect_phase_ms + span % 1000;
    uint32_t points = span / 1000 + phase / 1000;
    value->effect_phase_ms = phase % 1000;
    value->active_elapsed_ms += span;
    if (value->plan.type == DOMINO_BEHAVIOR_TYPE_PHONE_CALL)
        needs->energy = points > needs->energy ? 0 : (uint8_t)(needs->energy - points);
}
bool dominoTaskFixtureSupported(DOMINO_TASK_TYPE type) {
    return type == DOMINO_TASK_TYPE_WORK || type == DOMINO_TASK_TYPE_PHONE_CALL || type == DOMINO_TASK_TYPE_EXERCISE;
}
DominoTaskDecision dominoTaskFixtureDecide(const DominoTask* task, DominoTaskDecisionInput input) {
    if (task->execution.successful_behavior_count && (task->type != DOMINO_TASK_TYPE_EXERCISE || input.energy >= task->target_energy))
        return (DominoTaskDecision){DOMINO_TASK_SUCCEED, 0, true};
    return (DominoTaskDecision){task->execution.current_behavior_id || task->plan_count ? DOMINO_TASK_CONTINUE : DOMINO_TASK_REPAIR, 0, false};
}
/** @brief 可信加载夹具：只在无业务线程时恢复人物事实。 */
static DominoHuman* importHuman(uint32_t id, uint8_t energy, uint64_t manager) {
    DominoHumanData data = {.human_id = id,
                            .energy = energy,
                            .food = 100,
                            .water = 100,
                            .hygiene = 100,
                            .recreation = 100,
                            .social = 100,
                            .sleep_allowed = true,
                            .display_name_version = 1,
                            .manager_client_id = manager};
    DominoHuman human = {.human_id = id, .status = DOMINO_HUMAN_STATUS_THINKING};
    kv_push(DominoHumanData, domino_all_human_data_list, data);
    kv_push(DominoHuman, domino_all_human_list, human);
    int absent;
    khint_t key = dominoHumanIdMap_put(dominoHumanIdMap, id, &absent);
    CHECK(absent > 0);
    kh_val(dominoHumanIdMap, key) = (uint32_t)(kv_size(domino_all_human_list) - 1);
    for (size_t i = 0; i < kv_size(domino_all_human_list); ++i)
        kv_A(domino_all_human_list, i).data = &kv_A(domino_all_human_data_list, i);
    return dominoGetHumanByID(id);
}
static DominoHuman* fixture(uint8_t energy) {
    dominoHumanModuleInit();
    atomic_store(&g_domino_global_increment_id.motivation_increment_id, 0);
    atomic_store(&g_domino_global_increment_id.task_increment_id, 0);
    atomic_store(&g_domino_global_increment_id.behavior_increment_id, 0);
    memset(&g_fixture_runtime, 0, sizeof(g_fixture_runtime));
    for (uint8_t c = 0; c < 3; ++c)
        g_fixture_runtime.channel_version[c] = 1;
    DominoHuman* human = importHuman(1, energy, 1);
    human->runtime = &g_fixture_runtime;
    return human;
}
static void finishFixture(void) {
    CHECK(dominoHumanCandidateLiveBytes() == 0);
    dominoHumanModuleDestroy();
}
static uint32_t runPolicy(DominoHuman* human, DominoPlannerKind kind, uint32_t scope, uint32_t now) {
    DominoPlanOutcome outcome;
    DominoPlanCandidate* candidate =
        kind == DOMINO_PLANNER_MOTIVATION ? dominoHumanPlanMotivations(human, (DominoPlanScope){.object_id = scope}, now, 512, &outcome)
        : kind == DOMINO_PLANNER_TASK     ? dominoHumanPlanTasks(human, (DominoPlanScope){.object_id = scope}, now, 512, &outcome)
                                          : dominoHumanPlanBehaviors(human, (DominoPlanScope){.object_id = scope}, now, 512, &outcome);
    CHECK(outcome == DOMINO_PLAN_CANDIDATE && candidate);
    CHECK(dominoHumanAcceptCandidate(human, kind, (DominoPlanScope){.object_id = scope}, candidate, now));
    dominoHumanFreeCandidate(candidate);
    return kind == DOMINO_PLANNER_MOTIVATION ? human->data->motivation_ids[0]
           : kind == DOMINO_PLANNER_TASK     ? human->data->task_id_list[human->data->task_count - 1]
                                             : scope;
}
static DominoTask* importRunning(DominoHuman* human, uint32_t id, uint8_t channel, DOMINO_TASK_TYPE type, DOMINO_BEHAVIOR_TYPE behavior_type,
                                 uint32_t duration, uint8_t target) {
    DominoMotivation motivation = {.motivation_id = id,
                                   .owner_human_id = 1,
                                   .type = DOMINO_MOTIVATION_RECOVER_ENERGY,
                                   .status = DOMINO_MOTIVATION_VALID,
                                   .needs_action = true,
                                   .work_round = 1};
    CHECK(dominoHumanStoreImportMotivation(&motivation) == CODE_OK);
    human->data->motivation_ids[human->data->motivation_count++] = id;
    DominoBehavior behavior = {.behavior_id = id, .task_id = id, .plan = {duration, behavior_type}, .status = DOMINO_BEHAVIOR_STATUS_ACTIVE};
    CHECK(dominoHumanStoreImportBehavior(&behavior) == CODE_OK);
    DominoTask task = {.task_id = id,
                       .owner_human_id = 1,
                       .motivation_id = id,
                       .work_round = 1,
                       .plan_version = 1,
                       .arrangement_version = 1,
                       .first_record_id = id,
                       .record_count = 1,
                       .type = type,
                       .channel_index = channel,
                       .has_execution = true,
                       .target_energy = target,
                       .execution = {.current_behavior_id = id, .status = DOMINO_TASK_STATUS_OPEN}};
    CHECK(dominoHumanStoreImportTask(&task) == CODE_OK);
    human->data->task_id_list[human->data->task_count++] = id;
    human->data->active_task_id_list[channel] = id;
    atomic_store(&g_domino_global_increment_id.motivation_increment_id, id);
    atomic_store(&g_domino_global_increment_id.task_increment_id, id);
    atomic_store(&g_domino_global_increment_id.behavior_increment_id, id);
    return dominoGetTaskByID(id);
}
static void testSleepLifecycle(void) {
    DominoHuman* human = fixture(30);
    uint32_t motivation_id = runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0);
    uint32_t task_id = runPolicy(human, DOMINO_PLANNER_TASK, motivation_id, 0);
    CHECK(!dominoGetTaskByID(task_id)->has_execution && !human->data->active_task_id_list[0] && kv_size(domino_all_behavior_list) == 0);
    runPolicy(human, DOMINO_PLANNER_BEHAVIOR, task_id, 0);
    CHECK(!dominoGetTaskByID(task_id)->has_execution && kv_size(domino_all_behavior_list) == 0);
    dominoHumanStartReady(human, 0);
    DominoTask* task = dominoGetTaskByID(task_id);
    CHECK(task->has_execution && task->execution.started_at_ms == 0 && task->task_id == task_id);
    uint32_t behavior_id = task->execution.current_behavior_id;
    uint32_t duration = dominoGetBehaviorByID(behavior_id)->plan.duration_ms;
    dominoHumanSettle(human, duration + 2000);
    task = dominoGetTaskByID(task_id);
    CHECK(task->execution.status == DOMINO_TASK_STATUS_SUCCEEDED && task->execution.successful_behavior_count == 1);
    CHECK(task->execution.ended_at_ms == duration && human->data->settled_at_ms == duration + 2000);
    CHECK(dominoGetMotivationByID(motivation_id)->implementation_count == 1);
    CHECK(dominoGetMotivationByID(motivation_id)->history[0].ended_at_ms == duration);
    dominoHumanSettle(human, duration + 2000);
    CHECK(dominoGetMotivationByID(motivation_id)->implementation_count == 1);
    dominoHumanRecycle(human);
    CHECK(!dominoGetTaskByID(task_id) && !dominoGetBehaviorByID(behavior_id));
    CHECK(dominoGetMotivationByID(motivation_id)->closed_round == 1);
    finishFixture();
}
static void testConditions(void) {
    DominoHuman* human = fixture(30);
    uint32_t m = runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0);
    uint32_t t = runPolicy(human, DOMINO_PLANNER_TASK, m, 0);
    runPolicy(human, DOMINO_PLANNER_BEHAVIOR, t, 0);
    dominoHumanStartReady(human, 0);
    uint32_t b = dominoGetTaskByID(t)->execution.current_behavior_id;
    dominoHumanSettle(human, 6500);
    dominoHumanApplySleepCondition(human, false);
    dominoHumanCheckTasks(human, 6500, false);
    CHECK(dominoGetBehaviorByID(b)->status == DOMINO_BEHAVIOR_STATUS_CANCELLED);
    CHECK(dominoGetTaskByID(t)->execution.completed_active_ms == 6500 && dominoGetTaskByID(t)->execution.status == DOMINO_TASK_STATUS_OPEN);
    dominoHumanSettle(human, 15000);
    CHECK(dominoGetTaskByID(t)->execution.completed_active_ms == 6500 && human->data->energy == 31);
    dominoHumanApplySleepCondition(human, true);
    dominoHumanCheckTasks(human, 15000, false);
    runPolicy(human, DOMINO_PLANNER_BEHAVIOR, t, 15000);
    dominoHumanStartReady(human, 15000);
    CHECK(dominoGetTaskByID(t)->execution.current_behavior_id != b && dominoGetTaskByID(t)->execution.started_at_ms == 0);
    dominoHumanApplyEnergy(human, 80);
    dominoHumanCheckTasks(human, 15000, true);
    CHECK(dominoGetTaskByID(t)->execution.status == DOMINO_TASK_STATUS_CANCELLED && dominoGetMotivationByID(m)->implementation_count == 0);
    CHECK(dominoGetTaskByID(t)->execution.completed_active_ms == 6500);
    finishFixture();
    human = fixture(30);
    m = runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0);
    t = runPolicy(human, DOMINO_PLANNER_TASK, m, 0);
    dominoHumanApplyEnergy(human, 80);
    dominoHumanCheckTasks(human, 0, true);
    CHECK(!dominoGetTaskByID(t) && kv_size(domino_all_behavior_list) == 0 && dominoGetMotivationByID(m)->implementation_count == 0);
    finishFixture();
}
static void testIndependentAuxiliaryRepair(void) {
    DominoHuman* human = fixture(30);
    DominoTask* task = importRunning(human, 1, 2, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 12000, 80);
    dominoHumanSettle(human, 1000);
    dominoHumanApplySleepCondition(human, false);
    dominoHumanCheckTasks(human, 1000, false);
    CHECK(!task->execution.current_behavior_id && task->execution.completed_active_ms == 1000);
    dominoHumanSettle(human, 2000);
    dominoHumanApplySleepCondition(human, true);
    dominoHumanCheckTasks(human, 2000, false);
    runPolicy(human, DOMINO_PLANNER_BEHAVIOR, 1, 2000);
    dominoHumanStartReady(human, 2000);
    CHECK(!human->data->active_task_id_list[0] && human->data->active_task_id_list[2] == 1);
    CHECK(task->execution.current_behavior_id != 1 && task->execution.started_at_ms == 0);
    CHECK(dominoGetBehaviorByID(1)->status == DOMINO_BEHAVIOR_STATUS_CANCELLED);
    dominoHumanSettle(human, 8000);
    CHECK(human->data->energy == 31 && human->status == DOMINO_HUMAN_STATUS_NORMAL);
    finishFixture();
}
static void testHistory(void) {
    DominoHuman* human = fixture(30);
    uint32_t m = runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0), previous = 0, now = 0;
    for (uint32_t round = 1; round <= 11; ++round) {
        human->data->energy = 30;
        dominoGetMotivationByID(m)->reassess = true;
        runPolicy(human, DOMINO_PLANNER_MOTIVATION, m, now);
        uint32_t t = runPolicy(human, DOMINO_PLANNER_TASK, m, now);
        CHECK(t > previous);
        previous = t;
        runPolicy(human, DOMINO_PLANNER_BEHAVIOR, t, now);
        dominoHumanStartReady(human, now);
        uint32_t b = dominoGetTaskByID(t)->execution.current_behavior_id;
        now += dominoGetBehaviorByID(b)->plan.duration_ms;
        dominoHumanSettle(human, now);
        dominoHumanRecycle(human);
    }
    const DominoMotivation* motivation = dominoGetMotivationByID(m);
    CHECK(motivation->implementation_count == 11 && motivation->history_count == 10 && motivation->history[0].task_id == 2 &&
          motivation->closed_round == 11);
    finishFixture();
}
static void testSharedBoundary(void) {
    DominoHuman* human = fixture(79);
    importRunning(human, 1, 0, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 6000, 80);
    importRunning(human, 2, 1, DOMINO_TASK_TYPE_PHONE_CALL, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 6000, 0);
    dominoGetBehaviorByID(2)->effect_phase_ms = 0;
    dominoHumanSettle(human, 6000);
    CHECK(human->data->energy == 74 && dominoGetTaskByID(1)->execution.status == DOMINO_TASK_STATUS_OPEN);
    CHECK(dominoGetTaskByID(1)->execution.successful_behavior_count == 1);
    dominoHumanStoreRecycleBehaviors(dominoGetTaskByID(1));
    dominoHumanApplyEnergy(human, 80);
    dominoHumanCheckTasks(human, 6000, true);
    CHECK(dominoGetTaskByID(1)->execution.status == DOMINO_TASK_STATUS_SUCCEEDED);
    CHECK(dominoGetMotivationByID(1)->implementation_count == 1);
    finishFixture();
}
static void testDependencies(void) {
    for (uint8_t strict = 0; strict < 2; ++strict) {
        DominoHuman* human = fixture(20);
        importRunning(human, 1, 0, DOMINO_TASK_TYPE_WORK, DOMINO_BEHAVIOR_TYPE_WORK, 10000, 0);
        importRunning(human, 2, 1, DOMINO_TASK_TYPE_PHONE_CALL, DOMINO_BEHAVIOR_TYPE_CHAT, 10000, 0);
        importRunning(human, 3, 2, DOMINO_TASK_TYPE_EXERCISE, DOMINO_BEHAVIOR_TYPE_PHONE_CALL, 10000, 0);
        for (uint32_t id = 1; id <= 2; ++id) {
            DominoTask* task = dominoGetTaskByID(id);
            task->dependencies[0] =
                (DominoTaskDependency){.task_id = id + 1, .condition = DOMINO_DEPENDENCY_SUCCEEDED, .stages = DOMINO_DEPENDENCY_COMPLETE};
            task->dependency_count = 1;
        }
        dominoGetTaskByID(1)->finish_deadline = (DominoHumanCheckTime){10000, true};
        dominoGetTaskByID(1)->finish_inclusive = !strict;
        dominoHumanSettle(human, 12000);
        CHECK(dominoGetTaskByID(3)->execution.status == DOMINO_TASK_STATUS_SUCCEEDED);
        CHECK(dominoGetTaskByID(2)->execution.status == DOMINO_TASK_STATUS_SUCCEEDED);
        CHECK(dominoGetTaskByID(1)->execution.status == (strict ? DOMINO_TASK_STATUS_FAILED : DOMINO_TASK_STATUS_SUCCEEDED));
        CHECK(dominoGetTaskByID(1)->execution.ended_at_ms == 10000 && human->data->settled_at_ms == 12000);
        finishFixture();
    }
    DominoHuman* human = fixture(20);
    importRunning(human, 1, 0, DOMINO_TASK_TYPE_WORK, DOMINO_BEHAVIOR_TYPE_WORK, 10000, 0);
    importRunning(human, 2, 1, DOMINO_TASK_TYPE_PHONE_CALL, DOMINO_BEHAVIOR_TYPE_CHAT, 1000, 0);
    dominoGetTaskByID(1)->dependency_count = 1;
    dominoGetTaskByID(1)->dependencies[0] =
        (DominoTaskDependency){.task_id = 2, .condition = DOMINO_DEPENDENCY_ACTIVE, .stages = DOMINO_DEPENDENCY_CONTINUE};
    dominoHumanSettle(human, 2000);
    CHECK(dominoGetBehaviorByID(1)->status == DOMINO_BEHAVIOR_STATUS_CANCELLED && dominoGetBehaviorByID(1)->ended_at_ms == 1000);
    CHECK(dominoGetTaskByID(1)->execution.completed_active_ms == 1000 && dominoGetTaskByID(1)->waiting_condition);
    finishFixture();
}
static void testProjectionAndPartition(void) {
    DominoHuman* human = fixture(20);
    importRunning(human, 1, 0, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 50000, 100);
    importRunning(human, 2, 1, DOMINO_TASK_TYPE_PHONE_CALL, DOMINO_BEHAVIOR_TYPE_CHAT, 100000, 0);
    DominoNeedValues predicted;
    uint32_t budget = 4096;
    CHECK(dominoHumanPredictNeeds(human, 65000, &budget, &predicted));
    CHECK(human->data->settled_at_ms == 0 && dominoGetBehaviorByID(1)->active_elapsed_ms == 0);
    dominoHumanSettle(human, 35000);
    dominoHumanSettle(human, 65000);
    DominoNeedValues actual = dominoHumanReadNeeds(human->data);
    CHECK(predicted.energy == actual.energy && predicted.phase_ms == actual.phase_ms);
    CHECK(actual.energy == 27 && actual.phase_ms == 5000 && dominoGetBehaviorByID(1)->ended_at_ms == 50000);
    CHECK(dominoGetBehaviorByID(2)->active_elapsed_ms == 65000);
    finishFixture();
    human = fixture(20);
    importRunning(human, 1, 0, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 50000, 100);
    importRunning(human, 2, 1, DOMINO_TASK_TYPE_PHONE_CALL, DOMINO_BEHAVIOR_TYPE_CHAT, 100000, 0);
    dominoHumanSettle(human, 65000);
    CHECK(human->data->energy == actual.energy && human->data->needs_phase_ms == actual.phase_ms);
    finishFixture();
    DominoBehavior sample = {.plan = {10000, DOMINO_BEHAVIOR_TYPE_PHONE_CALL}, .effect_phase_ms = 900, .status = DOMINO_BEHAVIOR_STATUS_ACTIVE};
    DominoNeedValues needs = {.energy = 30};
    dominoBehaviorCalculate(&sample, &needs, 0, 200);
    CHECK(sample.effect_phase_ms == 100 && sample.active_elapsed_ms == 200 && needs.energy == 29);
    sample.status = DOMINO_BEHAVIOR_STATUS_WAITING;
    dominoBehaviorCalculate(&sample, &needs, 200, 400);
    CHECK(sample.effect_phase_ms == 100 && sample.active_elapsed_ms == 200 && needs.energy == 29);
}
static void testReadOnlyDisplayAndTimeRange(void) {
    DominoHuman* human = fixture(100);
    DominoNpcRequest request = {.session_id = 1, .request_id = 1, .human_id = 1, .expected_name_version = 1, .name_len = 3, .name = "Ada"};
    DominoNpcResult result;
    dominoHumanSetDisplayName(1, &request, &result);
    CHECK(result.status == DOMINO_NPC_NAME_CHANGED && human->data->display_name_version == 2);
    dominoHumanQueryNpc(1, &request, &result);
    CHECK(result.status == DOMINO_NPC_QUERY_COMPLETE && result.summary.settled_at_ms == 0);
    CHECK(!human->data->motivation_count && !human->data->task_count && !human->runtime->pending);
    dominoHumanSettle(human, UINT32_MAX);
    CHECK(human->data->settled_at_ms == UINT32_MAX && human->data->energy == 0 && human->data->needs_phase_ms == UINT32_MAX % 60000U);
    finishFixture();
}
static void testProjectionBoundaries(void) {
    for (uint8_t proof = 0; proof < 2; ++proof) {
        DominoHuman* human = fixture(79);
        DominoTask* task = importRunning(human, 1, 0, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 12000, 80);
        task->execution.successful_behavior_count = proof;
        DominoNeedValues predicted;
        uint32_t budget = 1;
        CHECK(!dominoHumanPredictNeeds(human, 13000, &budget, &predicted));
        budget = 512;
        CHECK(dominoHumanPredictNeeds(human, 13000, &budget, &predicted));
        dominoHumanSettle(human, 6000);
        CHECK(task->execution.status == (proof ? DOMINO_TASK_STATUS_SUCCEEDED : DOMINO_TASK_STATUS_OPEN));
        dominoHumanSettle(human, 13000);
        CHECK(human->data->energy == predicted.energy && task->execution.status == DOMINO_TASK_STATUS_SUCCEEDED);
        CHECK(task->execution.ended_at_ms == (proof ? 6000U : 12000U));
        CHECK(dominoGetMotivationByID(1)->implementation_count == 1);
        finishFixture();
    }
    DominoHuman* human = fixture(30);
    importRunning(human, 1, 0, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 12000, 80);
    importRunning(human, 2, 1, DOMINO_TASK_TYPE_WORK, DOMINO_BEHAVIOR_TYPE_WORK, 3000, 0);
    DominoTask* task = dominoGetTaskByID(1);
    task->dependency_count = 1;
    task->dependencies[0] = (DominoTaskDependency){.task_id = 2, .condition = DOMINO_DEPENDENCY_ACTIVE, .stages = DOMINO_DEPENDENCY_CONTINUE};
    uint32_t budget = 512;
    DominoNeedValues predicted;
    CHECK(dominoHumanPredictNeeds(human, 13000, &budget, &predicted));
    dominoHumanSettle(human, 13000);
    CHECK(predicted.energy == 30 && human->data->energy == predicted.energy && task->execution.completed_active_ms == 3000);
    finishFixture();
}
static void testParameterProtection(void) {
    DominoHuman* human = fixture(30);
    uint32_t m = runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0);
    uint32_t t = runPolicy(human, DOMINO_PLANNER_TASK, m, 0);
    DominoTask* task = dominoGetTaskByID(t);
    DominoTaskCandidate* arrangement = dominoHumanAllocateCandidate(sizeof(*arrangement) + sizeof(arrangement->items[0]), 1, 0);
    arrangement->items[0] = (DominoTaskSuggestion){.expected_arrangement_version = task->arrangement_version,
                                                   .expected_queue_version = human->runtime->channel_version[0],
                                                   .task_id = t,
                                                   .motivation_id = m,
                                                   .work_round = 1,
                                                   .type = DOMINO_TASK_TYPE_SLEEP,
                                                   .target_energy = 80,
                                                   .not_before = {100, true}};
    CHECK(dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = m}, &arrangement->header, 0));
    arrangement->items[0].not_before.at_ms = 200;
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = m}, &arrangement->header, 0));
    CHECK(task->not_before.at_ms == 100);
    dominoHumanFreeCandidate(&arrangement->header);
    runPolicy(human, DOMINO_PLANNER_BEHAVIOR, t, 0);
    dominoHumanSettle(human, 100);
    dominoHumanStartReady(human, 100);
    DominoBehavior* behavior = dominoGetBehaviorByID(task->execution.current_behavior_id);
    DominoBehaviorCandidate* adjustment = dominoHumanAllocateCandidate(sizeof(*adjustment) + sizeof(adjustment->items[0]), 1, 0);
    adjustment->items[0] = (DominoBehaviorSuggestion){.expected_plan_version = task->plan_version,
                                                      .task_id = t,
                                                      .expected_behavior_id = behavior->behavior_id,
                                                      .expected_duration_ms = behavior->plan.duration_ms,
                                                      .adjusted_duration_ms = 20000,
                                                      .depends_on_behavior = true,
                                                      .adjust_current = true};
    CHECK(dominoHumanAcceptCandidate(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = t}, &adjustment->header, 100));
    adjustment->items[0].adjusted_duration_ms = 25000;
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = t}, &adjustment->header, 100));
    dominoHumanSettle(human, 6600);
    adjustment->items[0].expected_duration_ms = 20000;
    adjustment->items[0].adjusted_duration_ms = 6499;
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = t}, &adjustment->header, 6600));
    adjustment->items[0].adjusted_duration_ms = 6500;
    CHECK(dominoHumanAcceptCandidate(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = t}, &adjustment->header, 6600));
    CHECK(task->execution.successful_behavior_count == 1 && task->execution.completed_active_ms == 6500 && !task->execution.current_behavior_id);
    dominoHumanFreeCandidate(&adjustment->header);
    finishFixture();
}
static void testCandidateProtection(void) {
    DominoHuman* human = fixture(30);
    uint32_t m = runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0);
    DominoPlanOutcome outcome;
    DominoPlanCandidate* task_plan = dominoHumanPlanTasks(human, (DominoPlanScope){.object_id = m}, 0, 512, &outcome);
    CHECK(dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = m}, task_plan, 0));
    CHECK(human->data->task_count == 1);
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = m}, task_plan, 0));
    dominoHumanFreeCandidate(task_plan);
    uint32_t t = human->data->task_id_list[0];
    DominoPlanCandidate* behavior_plan = dominoHumanPlanBehaviors(human, (DominoPlanScope){.object_id = t}, 0, 512, &outcome);
    CHECK(dominoHumanAcceptCandidate(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = t}, behavior_plan, 0));
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = t}, behavior_plan, 0));
    dominoHumanStartReady(human, 0);
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = t}, behavior_plan, 0));
    dominoHumanFreeCandidate(behavior_plan);
    human->runtime->requests[DOMINO_PLANNER_TASK] =
        (DominoPlanRequestState){.request_id = 1, .request_priority = DOMINO_PLAN_NORMAL, .deadline_ms = 3000};
    CHECK(dominoHumanPlannerMatches(human, DOMINO_PLANNER_TASK, DOMINO_PLAN_NORMAL, 1));
    CHECK(!dominoHumanPlannerMatches(human, DOMINO_PLANNER_TASK, DOMINO_PLAN_HIGH, 1));
    CHECK(!dominoHumanPlannerMatches(human, DOMINO_PLANNER_BEHAVIOR, DOMINO_PLAN_NORMAL, 1));
    finishFixture();
}

/** @brief 受控规划器提出主辅建议；正式记录仍只由候选接受入口创建。 */
static DominoTaskCandidate* controlledTasks(DominoHuman* human, bool invalid_required) {
    DominoTaskCandidate* candidate = dominoHumanAllocateCandidate(sizeof(*candidate) + 3 * sizeof(candidate->items[0]), 3, 0);
    for (uint8_t i = 0; i < 3; ++i) {
        candidate->items[i] = (DominoTaskSuggestion){.motivation_id = i + 1U,
                                                     .work_round = 1,
                                                     .type = i == 0   ? DOMINO_TASK_TYPE_SLEEP
                                                             : i == 1 ? DOMINO_TASK_TYPE_WORK
                                                                      : DOMINO_TASK_TYPE_PHONE_CALL,
                                                     .channel_index = i,
                                                     .expected_queue_version = human->runtime->channel_version[i],
                                                     .target_energy = i == 0 ? 80 : 0,
                                                     .queue_position = i == 2 || (invalid_required && i == 1) ? 2 : 0,
                                                     .optional = i == 2};
    }
    candidate->items[0].dependency_count = 1;
    candidate->items[0].dependency_local[0] = 1;
    candidate->items[0].dependencies[0] =
        (DominoTaskDependency){.condition = DOMINO_DEPENDENCY_ACTIVE, .stages = DOMINO_DEPENDENCY_START | DOMINO_DEPENDENCY_CONTINUE};
    return candidate;
}
static DominoBehaviorCandidate* controlledBehaviors(uint32_t main_id, uint32_t auxiliary_id) {
    size_t prefix = sizeof(DominoBehaviorCandidate) + 2 * sizeof(DominoBehaviorSuggestion);
    DominoBehaviorCandidate* candidate = dominoHumanAllocateCandidate(prefix + 2 * sizeof(DominoBehaviorPlan), 2, 2);
    uint32_t ids[] = {main_id, auxiliary_id};
    for (uint8_t i = 0; i < 2; ++i) {
        candidate->items[i] = (DominoBehaviorSuggestion){.task_id = ids[i],
                                                         .expected_plan_version = dominoGetTaskByID(ids[i])->plan_version,
                                                         .steps_offset = (uint32_t)(prefix + i * sizeof(DominoBehaviorPlan)),
                                                         .step_count = 1,
                                                         .replace_plans = true,
                                                         .depends_on_behavior = true,
                                                         .requires_unstarted = true};
        *(DominoBehaviorPlan*)((char*)candidate + candidate->items[i].steps_offset) =
            (DominoBehaviorPlan){i ? 3000 : 12000, i ? DOMINO_BEHAVIOR_TYPE_WORK : DOMINO_BEHAVIOR_TYPE_SLEEP};
    }
    return candidate;
}
static void testMultiObjectCandidates(void) {
    DominoHuman* human = fixture(30);
    for (uint32_t id = 1; id <= 3; ++id) {
        DominoMotivation motivation = {.motivation_id = id,
                                       .owner_human_id = 1,
                                       .type = DOMINO_MOTIVATION_RECOVER_ENERGY,
                                       .status = DOMINO_MOTIVATION_VALID,
                                       .needs_action = true};
        CHECK(dominoHumanStoreImportMotivation(&motivation) == CODE_OK);
        human->data->motivation_ids[human->data->motivation_count++] = id;
    }
    atomic_store(&g_domino_global_increment_id.motivation_increment_id, 3);
    DominoTaskCandidate* tasks = controlledTasks(human, true);
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = 1}, &tasks->header, 0));
    CHECK(!human->data->task_count && atomic_load(&g_domino_global_increment_id.task_increment_id) == 0);
    dominoHumanFreeCandidate(&tasks->header);
    tasks = controlledTasks(human, false);
    tasks->items[1].dependency_count = 1;
    tasks->items[1].dependency_local[0] = 0;
    tasks->items[1].dependencies[0] = (DominoTaskDependency){.condition = DOMINO_DEPENDENCY_SUCCEEDED, .stages = DOMINO_DEPENDENCY_START};
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = 1}, &tasks->header, 0));
    dominoHumanFreeCandidate(&tasks->header);
    tasks = controlledTasks(human, false);
    tasks->items[1].channel_index = 0;
    tasks->items[1].queue_position = 1;
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = 1}, &tasks->header, 0));
    CHECK(!human->data->task_count && atomic_load(&g_domino_global_increment_id.task_increment_id) == 0);
    dominoHumanFreeCandidate(&tasks->header);
    tasks = controlledTasks(human, false);
    tasks->items[2].dependency_count = 1;
    tasks->items[2].dependency_local[0] = 2;
    CHECK(!dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = 1}, &tasks->header, 0));
    CHECK(!human->data->task_count);
    dominoHumanFreeCandidate(&tasks->header);
    tasks = controlledTasks(human, false);
    CHECK(dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, (DominoPlanScope){.object_id = 1}, &tasks->header, 0));
    CHECK(human->data->task_count == 2 && !human->data->queue_count[2]);
    uint32_t main_id = human->data->channel_queue[0][0], auxiliary_id = human->data->channel_queue[1][0];
    CHECK(dominoGetTaskByID(main_id)->dependencies[0].task_id == auxiliary_id);
    dominoHumanFreeCandidate(&tasks->header);
    DominoBehaviorCandidate* behaviors = controlledBehaviors(main_id, auxiliary_id);
    CHECK(dominoHumanAcceptCandidate(human, DOMINO_PLANNER_BEHAVIOR, (DominoPlanScope){.object_id = main_id}, &behaviors->header, 0));
    dominoHumanFreeCandidate(&behaviors->header);
    dominoHumanStartReady(human, 0);
    CHECK(human->data->active_task_id_list[0] == main_id && human->data->active_task_id_list[1] == auxiliary_id);
    CHECK(dominoGetTaskByID(main_id)->execution.current_behavior_id && dominoGetTaskByID(auxiliary_id)->execution.current_behavior_id);
    dominoHumanSettle(human, 4000);
    CHECK(dominoGetTaskByID(auxiliary_id)->execution.status == DOMINO_TASK_STATUS_SUCCEEDED);
    CHECK(dominoGetTaskByID(main_id)->execution.status == DOMINO_TASK_STATUS_OPEN && dominoGetTaskByID(main_id)->waiting_condition);
    CHECK(dominoGetTaskByID(main_id)->execution.completed_active_ms == 3000 && !dominoGetTaskByID(main_id)->execution.current_behavior_id);
    finishFixture();
}
static void testResultQualification(void) {
    DominoHuman* human = fixture(30);
    DominoPlanOutcome outcome;
    human->runtime->requests[0] = (DominoPlanRequestState){.request_id = 9, .request_priority = DOMINO_PLAN_HIGH, .deadline_ms = 3000};
    DominoHumanPlanResultMessage result = {.human_id = 1,
                                           .planner_kind = DOMINO_PLANNER_MOTIVATION,
                                           .request_priority = DOMINO_PLAN_NORMAL,
                                           .request_id = 9,
                                           .outcome = DOMINO_PLAN_CANDIDATE};
    result.candidate = dominoHumanPlanMotivations(human, (DominoPlanScope){.object_id = 0}, 0, 512, &outcome);
    dominoHumanExecutionTestResult(&result);
    CHECK(human->runtime->requests[0].request_id == 9 && !human->data->motivation_count && dominoHumanCandidateLiveBytes() == 0);
    result.request_priority = DOMINO_PLAN_HIGH;
    result.candidate = dominoHumanPlanMotivations(human, (DominoPlanScope){.object_id = 0}, 0, 512, &outcome);
    human->runtime->requests[0].deadline_ms = 0;  // now == deadline 即过期，不创建动机。
    dominoHumanExecutionTestResult(&result);
    CHECK(!human->runtime->requests[0].request_id && !human->data->motivation_count && human->data->motivation_retry.valid);
    CHECK(human->data->motivation_retry.at_ms == 3000);
    finishFixture();
    human = fixture(30);
    dominoHumanPlannerTestFairness();
    dominoHumanPlannerTestLeaveCandidate(human);
    dominoHumanModuleDestroy();
    CHECK(dominoHumanCandidateLiveBytes() == 0);
}
static void pauseMs(uint32_t ms) {
    struct timespec delay = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000};
    CHECK(thrd_sleep(&delay, nullptr) == 0);
}
static DominoNpcResult requestNpc(uint64_t session, uint64_t id, bool rename, uint64_t version, const char* name) {
    DominoEngineMessage request = {.type = rename ? DOMINO_ENGINE_MSG_SET_NPC_NAME : DOMINO_ENGINE_MSG_QUERY_NPC,
                                   .payload.npc = {.session_id = session, .request_id = id, .expected_name_version = version, .human_id = 1}};
    if (name) {
        request.payload.npc.name_len = (uint16_t)strlen(name);
        memcpy(request.payload.npc.name, name, strlen(name) + 1);
    }
    CHECK(dominoEnginePushMsg(&request) == CODE_OK);
    for (uint32_t attempt = 0; attempt < 5000; ++attempt) {
        DominoEngineMessage response;
        DOMINO_CODE code = dominoEnginePopMsg(&response, false);
        if (code == CODE_OK) {
            CHECK(response.payload.result.request_id == id);
            return response.payload.result;
        }
        CHECK(code == ERR_QUEUE_EMPTY);
        pauseMs(1);
    }
    DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "NPC response timed out");
}
static void testEngineRecovery(void) {
    char path[] = "/tmp/domino-npc-ai-XXXXXX";
    CHECK(mkdtemp(path));
    DominoEngineLaunchConfig config = {.storage_path = path, .client_id = 1};
    dominoEngineInit(config);
    importHuman(1, 30, 1);  // 可信夹具在 Start 前恢复；重新准备首次只读调度。
    dominoHumanExecutionInitAfter();
    dominoEngineStop();
    dominoEngineDestroy();
    dominoEngineInit(config);
    CHECK(dominoGetHumanByID(1)->data->settled_at_ms == 0 && !dominoGetHumanByID(1)->data->task_count);
    DominoEngineClientData client;
    dominoEngineStart(&client);
    DominoNpcResult queried = requestNpc(client.session_id, 1, false, 0, nullptr);
    CHECK(queried.status == DOMINO_NPC_QUERY_COMPLETE && queried.summary.name_version == 1);
    DominoEngineMessage malformed = {.type = DOMINO_ENGINE_MSG_SET_NPC_NAME,
                                     .payload.npc = {.session_id = client.session_id,
                                                     .request_id = 99,
                                                     .human_id = 1,
                                                     .expected_name_version = 1,
                                                     .name_len = DOMINO_HUMAN_DISPLAY_NAME_MAX + 1}};
    CHECK(dominoEnginePushMsg(&malformed) == ERR_INVALID_PLAYER_NAME);
    malformed.type = DOMINO_ENGINE_MSG_NPC_RESULT;
    CHECK(dominoEnginePushMsg(&malformed) == ERR_INVALID_MESSAGE);
    DominoNpcResult renamed = requestNpc(client.session_id, 2, true, 1, "Ada");
    CHECK(renamed.status == DOMINO_NPC_NAME_CHANGED && renamed.summary.name_version == 2);
    CHECK(requestNpc(client.session_id, 3, true, 1, "old").status == DOMINO_NPC_VERSION_CONFLICT);
    CHECK(requestNpc(client.session_id, 4, true, 2, "Ada").status == DOMINO_NPC_UNCHANGED);
    CHECK(strcmp(requestNpc(client.session_id, 5, false, 0, nullptr).summary.name, "Ada") == 0);
    uint32_t task_id = 0, behavior_id = 0;
    for (uint32_t attempt = 0; attempt < 5000; ++attempt) {
        dominoHumanReadLock();
        const DominoHuman* human = dominoGetHumanByID(1);
        task_id = human->data->active_task_id_list[0];
        if (task_id)
            behavior_id = dominoGetTaskByID(task_id)->execution.current_behavior_id;
        dominoHumanReadUnlock();
        if (behavior_id)
            break;
        pauseMs(1);
    }
    CHECK(task_id && behavior_id);
    CHECK(dominoHumanUpdateSleepCondition(1, false) == CODE_OK);
    bool cancelled = false;
    for (uint32_t attempt = 0; attempt < 5000; ++attempt) {
        dominoHumanReadLock();
        const DominoHuman* human = dominoGetHumanByID(1);
        cancelled = !human->data->sleep_allowed && !dominoGetTaskByID(task_id)->execution.current_behavior_id;
        dominoHumanReadUnlock();
        if (cancelled)
            break;
        pauseMs(1);
    }
    CHECK(cancelled);
    dominoEngineFreezeTime();
    pauseMs(10);
    dominoEngineStop();
    uint32_t stopped_at;
    dominoTimeGetRuntimeMs(&stopped_at);
    CHECK(dominoGetHumanByID(1)->data->settled_at_ms == stopped_at);
    CHECK(dominoGetTaskByID(task_id)->execution.status == DOMINO_TASK_STATUS_OPEN);
    for (uint8_t k = 0; k < 3; ++k) {
        DominoPlannerMetrics metrics = dominoHumanPlannerMetrics((DominoPlannerKind)k);
        CHECK(metrics.requests[DOMINO_PLAN_NORMAL] > 0 && metrics.max_normal_batch <= 1 && metrics.max_high_batch <= 4);
        printf("planner %u: normal=%llu high=%llu batches=%llu read_lock_ns=%llu\n", k, (unsigned long long)metrics.requests[1],
               (unsigned long long)metrics.requests[0], (unsigned long long)metrics.batch_count, (unsigned long long)metrics.read_lock_ns);
    }
    dominoEngineDestroy();
    dominoEngineInit(config);
    CHECK(dominoGetTaskByID(task_id) && !dominoGetHumanByID(1)->data->sleep_allowed);
    CHECK(dominoGetHumanByID(1)->data->settled_at_ms == stopped_at);
    CHECK(dominoGetHumanByID(1)->data->display_name_version == 2 && strcmp(dominoGetHumanByID(1)->data->display_name, "Ada") == 0);
    uint64_t old_session = client.session_id;
    dominoEngineStart(&client);
    DominoEngineMessage old_request = {.type = DOMINO_ENGINE_MSG_QUERY_NPC,
                                       .payload.npc = {.session_id = old_session, .request_id = 100, .human_id = 1}};
    CHECK(dominoEnginePushMsg(&old_request) == ERR_INVALID_PARAM);
    CHECK(dominoHumanUpdateSleepCondition(1, true) == CODE_OK);
    uint32_t replacement = 0;
    for (uint32_t attempt = 0; attempt < 5000; ++attempt) {
        dominoHumanReadLock();
        replacement = dominoGetTaskByID(task_id)->execution.current_behavior_id;
        dominoHumanReadUnlock();
        if (replacement)
            break;
        pauseMs(1);
    }
    CHECK(replacement && replacement != behavior_id);
    dominoEngineStop();
    dominoEngineDestroy();
    config.client_id = 2;
    dominoEngineInit(config);
    dominoEngineStart(&client);
    CHECK(requestNpc(client.session_id, 10, false, 0, nullptr).status == DOMINO_NPC_REJECTED);
    CHECK(requestNpc(client.session_id, 11, true, 2, "intruder").status == DOMINO_NPC_REJECTED);
    dominoEngineStop();
    dominoEngineDestroy();
    printf("recovery save: %s\n", path);
}

static void testPopulation(void) {
    for (uint32_t quota = 1; quota <= 4; quota *= 4) {
        char path[] = "/tmp/domino-npc-load-XXXXXX";
        CHECK(mkdtemp(path));
        DominoEngineLaunchConfig config = {.storage_path = path};
        for (uint8_t k = 0; k < 3; ++k)
            config.planners[k] = (DominoPlannerConfig){quota, 3000, 512};
        dominoEngineInit(config);
        for (uint32_t id = 1; id <= 1000; ++id)
            importHuman(id, id % 2 ? 30 : 5, 1);
        dominoHumanExecutionInitAfter();
        DominoEngineClientData client;
        dominoEngineStart(&client);
        for (uint32_t i = 0; i < 300; ++i)
            CHECK(dominoHumanUpdateSleepCondition(1, (i & 1U) != 0) == CODE_OK);
        DominoTimingSamples query_latency = {0};
        for (uint32_t i = 0; i < 20; ++i) {
            uint64_t begin = dominoMonotonicTimeNs();
            CHECK(requestNpc(client.session_id, 1000 + i, false, 0, nullptr).status == DOMINO_NPC_QUERY_COMPLETE);
            dominoTimingRecord(&query_latency, dominoMonotonicTimeNs() - begin);
        }
        bool ready = false;
        for (uint32_t i = 0; i < 5000; ++i) {
            dominoHumanReadLock();
            const DominoHuman* human = dominoGetHumanByID(1000);
            ready = human->data->active_task_id_list[0] != 0;
            for (uint32_t id = 1; ready && id <= 1000; ++id)
                ready = dominoGetHumanByID(id)->data->active_task_id_list[0] != 0;
            dominoHumanReadUnlock();
            if (ready)
                break;
            pauseMs(1);
        }
        CHECK(ready);
        for (uint32_t i = 0; i < 10; ++i) {
            dominoEngineFreezeTime();
            dominoEngineThawTime();
        }
        dominoEngineFreezeTime();
        dominoEngineStop();
        DominoExecutionMetrics execution = dominoHumanExecutionMetrics();
        CHECK(execution.condition_latency.count == 300);
        printf(
            "load K=%u humans=1000 results=%llu rejected=%llu stale=%llu write_p99_ns=%llu condition_p95_ns=%llu condition_p99_ns=%llu "
            "candidate_peak_bytes=%zu\n",
            quota, (unsigned long long)execution.results, (unsigned long long)execution.rejected, (unsigned long long)execution.stale,
            (unsigned long long)dominoTimingPercentile(&execution.write_lock, 99),
            (unsigned long long)dominoTimingPercentile(&execution.condition_latency, 95),
            (unsigned long long)dominoTimingPercentile(&execution.condition_latency, 99), dominoHumanCandidatePeakBytes());
        DominoCandidateMetrics allocations = dominoHumanCandidateMetrics();
        DominoPlanCopyMetrics copies = dominoHumanStoreCopyMetrics();
        printf("load memory K=%u allocations=%llu allocated_bytes=%llu allocation_ns=%llu copied_bytes=%llu copy_p99_ns=%llu query_p99_ns=%llu\n",
               quota, (unsigned long long)allocations.allocations, (unsigned long long)allocations.allocated_bytes,
               (unsigned long long)allocations.allocation_ns, (unsigned long long)copies.bytes,
               (unsigned long long)dominoTimingPercentile(&copies.timing, 99), (unsigned long long)dominoTimingPercentile(&query_latency, 99));
        for (uint8_t k = 0; k < 3; ++k) {
            DominoPlannerMetrics metrics = dominoHumanPlannerMetrics((DominoPlannerKind)k);
            CHECK(metrics.requests[0] >= 500 && metrics.requests[1] >= 500 && metrics.max_normal_batch == 1 && metrics.max_high_batch <= quota);
            for (uint8_t priority = 0; priority < 2; ++priority)
                printf(
                    "load planner=%u priority=%u K=%u requests=%llu queue_p99_ns=%llu request_p99_ns=%llu result_p99_ns=%llu batch_p99_ns=%llu "
                    "lock_p99_ns=%llu\n",
                    k, priority, quota, (unsigned long long)metrics.requests[priority],
                    (unsigned long long)dominoTimingPercentile(&metrics.queue_samples[priority], 99),
                    (unsigned long long)dominoTimingPercentile(&metrics.request_samples[priority], 99),
                    (unsigned long long)dominoTimingPercentile(&execution.result_wait[k][priority], 99),
                    (unsigned long long)dominoTimingPercentile(&metrics.batch_samples, 99),
                    (unsigned long long)dominoTimingPercentile(&metrics.lock_samples, 99));
        }
        dominoEngineDestroy();
        dominoEngineInit(config);
        CHECK(kv_size(domino_all_human_list) == 1000 && kv_size(domino_all_motivation_list) == 1000);
        dominoEngineStop();
        dominoEngineDestroy();
    }
}
static void testStoppedPlanning(void) {
    char path[] = "/tmp/domino-npc-stop-XXXXXX";
    CHECK(mkdtemp(path));
    DominoEngineLaunchConfig config = {.storage_path = path};
    dominoEngineInit(config);
    for (uint32_t id = 1; id <= 100; ++id)
        importHuman(id, 30, 1);
    dominoHumanExecutionInitAfter();
    DominoEngineClientData client;
    dominoEngineStart(&client);
    dominoEngineStop();
    uint32_t accepted[100], now;
    dominoTimeGetRuntimeMs(&now);
    for (uint32_t id = 1; id <= 100; ++id) {
        const DominoHumanData* data = dominoGetHumanByID(id)->data;
        CHECK(data->settled_at_ms == now && data->task_count <= 1);
        accepted[id - 1] = data->task_count ? data->task_id_list[0] : 0;
    }
    dominoEngineDestroy();
    CHECK(dominoHumanCandidateLiveBytes() == 0);
    dominoEngineInit(config);
    dominoEngineStart(&client);
    bool ready = false;
    for (uint32_t i = 0; i < 5000; ++i) {
        ready = true;
        dominoHumanReadLock();
        for (uint32_t id = 1; id <= 100; ++id)
            ready &= dominoGetHumanByID(id)->data->active_task_id_list[0] != 0;
        dominoHumanReadUnlock();
        if (ready)
            break;
        pauseMs(1);
    }
    CHECK(ready);
    dominoEngineStop();
    CHECK(kv_size(domino_all_task_list) == 100 && kv_size(domino_all_motivation_list) == 100);
    for (uint32_t id = 1; id <= 100; ++id)
        if (accepted[id - 1])
            CHECK(dominoGetHumanByID(id)->data->active_task_id_list[0] == accepted[id - 1]);
    dominoEngineDestroy();
}
static void testSavedStages(void) {
    for (uint8_t stage = 0; stage < 7; ++stage) {
        char path[] = "/tmp/domino-npc-stage-XXXXXX";
        CHECK(mkdtemp(path));
        DominoEngineLaunchConfig config = {.storage_path = path};
        dominoEngineInit(config);
        DominoHuman* human = importHuman(1, stage == 3 ? 20 : 30, 1);
        memset(&g_fixture_runtime, 0, sizeof(g_fixture_runtime));
        for (uint8_t c = 0; c < 3; ++c)
            g_fixture_runtime.channel_version[c] = 1;
        human->runtime = &g_fixture_runtime;
        uint32_t t = 0, at = 0;
        if (stage < 3) {
            uint32_t m = runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0);
            if (stage >= 1)
                t = runPolicy(human, DOMINO_PLANNER_TASK, m, 0);
            if (stage == 2)
                runPolicy(human, DOMINO_PLANNER_BEHAVIOR, t, 0);
        } else if (stage == 3) {
            importRunning(human, 1, 0, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 50000, 100);
            importRunning(human, 2, 1, DOMINO_TASK_TYPE_PHONE_CALL, DOMINO_BEHAVIOR_TYPE_CHAT, 100000, 0);
            dominoHumanSettle(human, 35000);
            at = 35000;
        } else if (stage == 4) {
            importRunning(human, 1, 0, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 12000, 80);
            importRunning(human, 2, 1, DOMINO_TASK_TYPE_WORK, DOMINO_BEHAVIOR_TYPE_WORK, 3000, 0);
            DominoTask* task = dominoGetTaskByID(1);
            task->dependency_count = 1;
            task->dependencies[0] = (DominoTaskDependency){.task_id = 2, .condition = DOMINO_DEPENDENCY_ACTIVE, .stages = DOMINO_DEPENDENCY_CONTINUE};
        } else {
            importRunning(human, 1, 0, DOMINO_TASK_TYPE_SLEEP, DOMINO_BEHAVIOR_TYPE_SLEEP, 6000, 80);
            dominoHumanSettle(human, 6000);
            at = 6000;
            if (stage == 6)
                dominoHumanStoreRecycleBehaviors(dominoGetTaskByID(1));
        }
        dominoSetRuntimeDateTimeBase((uint64_t)at * 1000000);
        dominoHumanExecutionInitAfter();
        dominoEngineStop();
        dominoEngineDestroy();
        dominoEngineInit(config);
        human = dominoGetHumanByID(1);
        CHECK(human->data->settled_at_ms == at && human->data->motivation_count != 0);
        if (stage < 3) {
            if (stage)
                CHECK(dominoGetTaskByID(t) && !dominoGetTaskByID(t)->has_execution);
            if (stage == 2)
                CHECK(dominoGetTaskByID(t)->plan_count == 1 && kv_size(domino_all_behavior_list) == 0);
            DominoEngineClientData client;
            dominoEngineStart(&client);
            bool ready = false;
            for (uint32_t i = 0; i < 5000; ++i) {
                dominoHumanReadLock();
                ready = human->data->active_task_id_list[0] != 0;
                dominoHumanReadUnlock();
                if (ready)
                    break;
                pauseMs(1);
            }
            CHECK(ready);
        } else if (stage == 3) {
            CHECK(human->data->needs_phase_ms == 35000 && dominoGetBehaviorByID(1)->effect_phase_ms == 5000);
            dominoHumanSettle(human, 65000);
            dominoSetRuntimeDateTimeBase(UINT64_C(65000000000));
            CHECK(human->data->energy == 27 && human->data->needs_phase_ms == 5000);
            CHECK(dominoGetBehaviorByID(1)->ended_at_ms == 50000 && dominoGetBehaviorByID(2)->active_elapsed_ms == 65000);
        } else if (stage == 4) {
            CHECK(dominoGetTaskByID(1)->dependencies[0].task_id == 2);
            dominoHumanSettle(human, 4000);
            dominoSetRuntimeDateTimeBase(UINT64_C(4000000000));
            CHECK(dominoGetTaskByID(1)->waiting_condition && dominoGetTaskByID(1)->execution.completed_active_ms == 3000);
            CHECK(dominoGetMotivationByID(2)->implementation_count == 1);
        } else {
            DominoTask* task = dominoGetTaskByID(1);
            CHECK(task->execution.successful_behavior_count == 1 && task->execution.completed_active_ms == 6000);
            CHECK(task->record_count == (stage == 5 ? 1 : 0));
            dominoHumanApplyEnergy(human, 80);
            dominoHumanCheckTasks(human, 6000, true);
            dominoHumanCheckTasks(human, 6000, false);
            CHECK(task->execution.status == DOMINO_TASK_STATUS_SUCCEEDED && dominoGetMotivationByID(1)->implementation_count == 1);
        }
        dominoEngineStop();
        dominoEngineDestroy();
    }
}
static void testTaskWindowRepair(void) {
    DominoHuman* human = fixture(30);
    uint32_t m = runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0);
    uint32_t t = runPolicy(human, DOMINO_PLANNER_TASK, m, 0);
    runPolicy(human, DOMINO_PLANNER_BEHAVIOR, t, 0);
    DominoTask* task = dominoGetTaskByID(t);
    task->not_before = (DominoHumanCheckTime){10000, true};
    task->start_deadline = (DominoHumanCheckTime){1000, true};
    dominoHumanSettle(human, 2000);
    CHECK(task->task_id == t && !task->has_execution && !task->plan_count);
    CHECK(dominoTaskQuery(task, human->data, 2000).needs_arrangement);
    DominoPlanOutcome outcome;
    DominoPlanScope scope = {.object_id = t, .accepted_task = true};
    DominoPlanCandidate* candidate = dominoHumanPlanTasks(human, scope, 2000, 512, &outcome);
    CHECK(outcome == DOMINO_PLAN_CANDIDATE && dominoHumanAcceptCandidate(human, DOMINO_PLANNER_TASK, scope, candidate, 2000));
    dominoHumanFreeCandidate(candidate);
    CHECK(!task->start_deadline.valid && task->not_before.at_ms == 10000 && !task->has_execution);
    runPolicy(human, DOMINO_PLANNER_BEHAVIOR, t, 2000);
    dominoHumanStartReady(human, 2000);
    CHECK(!task->has_execution);
    dominoHumanSettle(human, 10000);
    dominoHumanStartReady(human, 10000);
    CHECK(task->execution.started_at_ms == 10000 && task->task_id == t);
    finishFixture();
}
static _Atomic uint32_t g_smoke_value;
static int smokeThread(void* ignored) {
    (void)ignored;
    atomic_store(&g_smoke_value, 1);
    return 0;
}
static int runThreadSmoke(void) {
    thrd_t thread;
    if (thrd_create(&thread, smokeThread, nullptr) != thrd_success || thrd_join(thread, nullptr) != thrd_success)
        return 2;
    return atomic_load(&g_smoke_value) == 1 ? 0 : 3;
}
static int illegalClientThread(void* ignored) {
    (void)ignored;
    dominoEngineStart(nullptr);
    return 0;
}
static void expectedFailure(const char* mode, const char* path) {
    if (strcmp(mode, "--illegal-start") == 0)
        dominoEngineStart(nullptr);
    if (strcmp(mode, "--time-overflow") == 0)
        (void)dominoHumanTimeAdd(UINT32_MAX, 1);
    if (strcmp(mode, "--id-overflow") == 0) {
        DominoHuman* human = fixture(30);
        atomic_store(&g_domino_global_increment_id.motivation_increment_id, UINT32_MAX);
        runPolicy(human, DOMINO_PLANNER_MOTIVATION, 0, 0);
    }
    char temporary[] = "/tmp/domino-npc-invalid-XXXXXX";
    if (!path) {
        CHECK(mkdtemp(temporary));
        path = temporary;
    }
    DominoEngineLaunchConfig config = {.storage_path = path};
    if (strcmp(mode, "--invalid-quota") == 0)
        config.planners[0] = (DominoPlannerConfig){0, 3000, 512};
    dominoEngineInit(config);
    if (strcmp(mode, "--illegal-destroy") == 0)
        dominoEngineDestroy();
    if (strcmp(mode, "--illegal-thread") == 0) {
        thrd_t thread;
        CHECK(thrd_create(&thread, illegalClientThread, nullptr) == thrd_success);
        CHECK(thrd_join(thread, nullptr) == thrd_success);
    }
    if (strcmp(mode, "--load") == 0) {
        dominoEngineStop();
        dominoEngineDestroy();
        return;
    }
    fputs("expected rejection did not occur\n", stderr);
    exit(42);
}
int dominoHumanAIPlayground(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--thread-smoke") == 0)
        return runThreadSmoke();
    if (argc > 1) {
        expectedFailure(argv[1], argc > 2 ? argv[2] : nullptr);
        return 0;
    }
    testSleepLifecycle();
    testConditions();
    testIndependentAuxiliaryRepair();
    testHistory();
    testSharedBoundary();
    testDependencies();
    testReadOnlyDisplayAndTimeRange();
    testProjectionAndPartition();
    testProjectionBoundaries();
    testParameterProtection();
    testCandidateProtection();
    testMultiObjectCandidates();
    testResultQualification();
    testTaskWindowRepair();
    testStoppedPlanning();
    testSavedStages();
    testEngineRecovery();
    testPopulation();
    puts("NPC AI acceptance tests passed");
    return 0;
}
