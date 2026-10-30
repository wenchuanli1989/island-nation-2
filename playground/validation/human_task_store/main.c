#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "../../../engine/src/host/entry.h"
#include "../../../engine/src/human/entry.h"
#include "../../../engine/src/human/rules.h"
#include "../../../engine/src/human/store.h"
#include "../../../engine/src/human/task/sleep.h"
#include "../../../engine/src/time/entry.h"
#include "domino_engine.h"
#include "domino_shared_common.h"

#define CHECK(condition)                                                                                    \
    do {                                                                                                    \
        if (!(condition)) {                                                                                 \
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "human task store regression failed: %s", #condition); \
        }                                                                                                   \
    } while (0)

#define CHECK_CODE(expression) CHECK((expression) == CODE_OK)
#define CHECK_FIELD(actual, expected, field) CHECK((actual)->field == (expected)->field)

typedef struct TaskSnapshot {
    DominoTask task;
    DominoBehavior behaviors[DOMINO_TASK_BEHAVIOR_COUNT_MAX];
    bool retained_flag;
} TaskSnapshot;

/** @brief 仅在 Init 后、业务线程尚未启动时注入测试时钟夹具；输入本身为已校验的 32 位业务毫秒。 */
static void setFixtureTimeMs(domino_runtime_ms_t now_ms) {
    CHECK(dominoTimeModuleIsFrozen());
    dominoSetRuntimeDateTimeBase((uint64_t)now_ms * UINT64_C(1000000));
    domino_runtime_ms_t sampled_ms;
    dominoTimeGetRuntimeMs(&sampled_ms);
    CHECK(sampled_ms == now_ms);
}

static void checkBehavior(const DominoBehavior* actual_ptr, const DominoBehavior* expected_ptr) {
    CHECK(actual_ptr != nullptr);
    CHECK_FIELD(actual_ptr, expected_ptr, behavior_id);
    CHECK_FIELD(actual_ptr, expected_ptr, type);
    CHECK_FIELD(actual_ptr, expected_ptr, status);
    CHECK_FIELD(actual_ptr, expected_ptr, planned_begin_ms);
    CHECK_FIELD(actual_ptr, expected_ptr, planned_end_ms);
    CHECK_FIELD(actual_ptr, expected_ptr, started_at_ms);
    CHECK_FIELD(actual_ptr, expected_ptr, ended_at_ms);
    CHECK_FIELD(actual_ptr, expected_ptr, accounted_at_ms);
    CHECK_FIELD(actual_ptr, expected_ptr, active_elapsed_ms);
    CHECK_FIELD(actual_ptr, expected_ptr, started_flag);
}

static void captureTask(domino_task_id_t task_id, TaskSnapshot* snapshot_ptr) {
    const DominoTask* task_ptr = dominoGetTaskByID(task_id);
    CHECK(task_ptr != nullptr);
    snapshot_ptr->task = *task_ptr;
    snapshot_ptr->retained_flag = true;
    for (uint32_t index = 0U; index < task_ptr->behavior_count; index++) {
        snapshot_ptr->behaviors[index] = *dominoGetBehaviorByID(task_ptr->behavior_id_list[index]);
    }
}

static void checkTask(const TaskSnapshot* snapshot_ptr, bool reloaded_flag) {
    const DominoTask* task_ptr = dominoGetTaskByID(snapshot_ptr->task.task_id);
    CHECK(task_ptr != nullptr);
    const DominoTask* expected_ptr = &snapshot_ptr->task;
    CHECK_FIELD(task_ptr, expected_ptr, task_id);
    CHECK_FIELD(task_ptr, expected_ptr, owner_human_id);
    CHECK_FIELD(task_ptr, expected_ptr, current_behavior_id);
    CHECK_FIELD(task_ptr, expected_ptr, behavior_count);
    CHECK_FIELD(task_ptr, expected_ptr, type);
    CHECK_FIELD(task_ptr, expected_ptr, status);
    CHECK_FIELD(task_ptr, expected_ptr, priority_class);
    CHECK_FIELD(task_ptr, expected_ptr, channel_index);
    CHECK_FIELD(task_ptr, expected_ptr, planned_begin_ms);
    CHECK_FIELD(task_ptr, expected_ptr, planned_end_ms);
    CHECK_FIELD(task_ptr, expected_ptr, started_at_ms);
    CHECK_FIELD(task_ptr, expected_ptr, ended_at_ms);
    CHECK_FIELD(task_ptr, expected_ptr, started_flag);
    CHECK(task_ptr->execution_version == (reloaded_flag ? 1U : expected_ptr->execution_version));
    for (uint32_t index = 0U; index < task_ptr->behavior_count; index++) {
        CHECK(task_ptr->behavior_id_list[index] == expected_ptr->behavior_id_list[index]);
        const DominoBehavior* behavior_ptr = dominoGetBehaviorByID(task_ptr->behavior_id_list[index]);
        checkBehavior(behavior_ptr, &snapshot_ptr->behaviors[index]);
    }
}

static void checkStore(const TaskSnapshot snapshots[5], bool reloaded_flag) {
    const DominoHumanData* data_ptr = dominoGetHumanDataByID(1U);
    size_t task_count = 0U;
    size_t behavior_count = 0U;
    for (size_t index = 0U; index < 5U; index++) {
        const TaskSnapshot* snapshot_ptr = &snapshots[index];
        if (!snapshot_ptr->retained_flag) {
            CHECK(dominoGetTaskByID(snapshot_ptr->task.task_id) == nullptr);
            for (uint32_t sequence_index = 0U; sequence_index < snapshot_ptr->task.behavior_count; sequence_index++) {
                CHECK(dominoGetBehaviorByID(snapshot_ptr->task.behavior_id_list[sequence_index]) == nullptr);
            }
            continue;
        }
        CHECK(data_ptr->task_id_list[task_count++] == snapshot_ptr->task.task_id);
        behavior_count += snapshot_ptr->task.behavior_count;
        checkTask(snapshot_ptr, reloaded_flag);
    }
    CHECK(data_ptr->task_count == task_count);
    CHECK(kv_size(domino_all_task_list) == task_count);
    CHECK(kv_size(domino_all_behavior_list) == behavior_count);
}

/** @brief 构造受控工作草案，并验证 store 复制内容而非借用草案数组。 */
static void appendWorkTask(uint8_t behavior_count, uint32_t marker, TaskSnapshot* snapshot_ptr) {
    DominoBehavior draft_behaviors[DOMINO_TASK_BEHAVIOR_COUNT_MAX];
    for (uint32_t index = 0U; index < behavior_count; index++) {
        const domino_runtime_ms_t begin_ms = 100U + marker * 100U + index;
        draft_behaviors[index] = (DominoBehavior){
            .planned_begin_ms = begin_ms,
            .planned_end_ms = begin_ms + 50U,
            .type = DOMINO_BEHAVIOR_TYPE_WORK,
            .status = DOMINO_BEHAVIOR_STATUS_PENDING,
        };
    }
    DominoTask draft = {
        .owner_human_id = 1U,
        .planned_begin_ms = draft_behaviors[0].planned_begin_ms,
        .planned_end_ms = draft_behaviors[behavior_count - 1U].planned_end_ms,
        .type = DOMINO_TASK_TYPE_WORK,
        .behavior_count = behavior_count,
        .status = DOMINO_TASK_STATUS_PLANNED,
        .priority_class = DOMINO_TASK_PRIORITY_COMMITTED,
    };
    domino_task_id_t task_id;
    CHECK_CODE(dominoHumanStoreAddValidatedTask(&draft, draft_behaviors, dominoGetHumanDataByID(1U), &task_id));
    memset(draft_behaviors, 0, sizeof(draft_behaviors));
    captureTask(task_id, snapshot_ptr);
    CHECK(snapshot_ptr->task.current_behavior_id == snapshot_ptr->task.behavior_id_list[0]);
    for (uint32_t index = 0U; index < behavior_count; index++) {
        CHECK(snapshot_ptr->behaviors[index].type == DOMINO_BEHAVIOR_TYPE_WORK);
        CHECK(snapshot_ptr->behaviors[index].planned_begin_ms == 100U + marker * 100U + index);
    }
}

/** @brief 无业务线程的夹具中构造一致终态，再通过正式回收入口删除；不测试工作执行器。 */
static void finishAndRemoveTask(domino_task_id_t task_id, domino_runtime_ms_t now_ms) {
    DominoTask* task_ptr = dominoGetTaskByID(task_id);
    for (uint32_t index = 0U; index < task_ptr->behavior_count; index++) {
        DominoBehavior* behavior_ptr = dominoGetBehaviorByID(task_ptr->behavior_id_list[index]);
        behavior_ptr->status = DOMINO_BEHAVIOR_STATUS_DONE;
        behavior_ptr->started_flag = true;
        behavior_ptr->started_at_ms = behavior_ptr->planned_begin_ms;
        behavior_ptr->ended_at_ms = now_ms;
        behavior_ptr->accounted_at_ms = now_ms;
        behavior_ptr->active_elapsed_ms = behavior_ptr->planned_end_ms - behavior_ptr->planned_begin_ms;
    }
    task_ptr->status = DOMINO_TASK_STATUS_SUCCEEDED;
    task_ptr->current_behavior_id = 0U;
    task_ptr->started_flag = true;
    task_ptr->started_at_ms = task_ptr->planned_begin_ms;
    task_ptr->ended_at_ms = now_ms;
    CHECK_CODE(dominoHumanStoreRemoveTask(task_id));
}

static void checkDestroyedStore(void) {
    CHECK(kv_size(domino_all_task_list) == 0U && kv_max(domino_all_task_list) == 0U && domino_all_task_list.a == nullptr);
    CHECK(kv_size(domino_all_behavior_list) == 0U && kv_max(domino_all_behavior_list) == 0U && domino_all_behavior_list.a == nullptr);
    CHECK(atomic_load(&g_domino_global_increment_id.task_increment_id) == 0U);
    CHECK(atomic_load(&g_domino_global_increment_id.behavior_increment_id) == 0U);
}

int main(int argc, char* argv[]) {
    CHECK(argc == 2 && argv[1][0] == '/');
    const DominoEngineLaunchConfig config = {.storage_path = argv[1], .storage_integrity_verify = true};
    dominoEngineInit(config);
    CHECK(dominoGetHumanByID(1U) != nullptr);
    CHECK(kv_size(domino_all_task_list) == 0U && kv_size(domino_all_behavior_list) == 0U);
    CHECK(dominoGetHumanDataByID(1U)->task_count == 0U);
    atomic_store(&g_domino_global_increment_id.task_increment_id, 0U);
    atomic_store(&g_domino_global_increment_id.behavior_increment_id, 0U);
    TaskSnapshot snapshots[5];
    appendWorkTask(3U, 1U, &snapshots[0]);
    appendWorkTask(DOMINO_TASK_BEHAVIOR_COUNT_MAX, 2U, &snapshots[1]);
    appendWorkTask(3U, 3U, &snapshots[2]);
    atomic_store(&g_domino_global_increment_id.task_increment_id, UINT32_MAX);
    atomic_store(&g_domino_global_increment_id.behavior_increment_id, UINT32_MAX);
    appendWorkTask(1U, 4U, &snapshots[3]);
    appendWorkTask(1U, 5U, &snapshots[4]);
    CHECK(snapshots[3].task.task_id == UINT32_MAX && snapshots[3].behaviors[0].behavior_id == UINT32_MAX);
    CHECK(snapshots[4].task.task_id == 4U && snapshots[4].behaviors[0].behavior_id == 262U);
    dominoGetTaskByID(snapshots[1].task.task_id)->execution_version = 9U;
    snapshots[1].task.execution_version = 9U;
    checkStore(snapshots, false);
    setFixtureTimeMs(2000U);
    finishAndRemoveTask(snapshots[0].task.task_id, 2000U);
    snapshots[0].retained_flag = false;
    checkStore(snapshots, false);
    const domino_task_id_t task_cursor = atomic_load(&g_domino_global_increment_id.task_increment_id);
    const domino_behavior_id_t behavior_cursor = atomic_load(&g_domino_global_increment_id.behavior_increment_id);
    dominoEngineStop();
    dominoEngineDestroy();
    checkDestroyedStore();

    dominoEngineInit(config);
    checkStore(snapshots, true);
    CHECK(atomic_load(&g_domino_global_increment_id.task_increment_id) == task_cursor);
    CHECK(atomic_load(&g_domino_global_increment_id.behavior_increment_id) == behavior_cursor);
    finishAndRemoveTask(snapshots[1].task.task_id, 2000U);
    snapshots[1].retained_flag = false;
    checkStore(snapshots, true);
    for (size_t index = 2U; index < 5U; index++) {
        finishAndRemoveTask(snapshots[index].task.task_id, 2000U);
    }
    CHECK(kv_size(domino_all_task_list) == 0U && kv_size(domino_all_behavior_list) == 0U);

    DominoHuman* human_ptr = dominoGetHumanByID(1U);
    domino_task_id_t sleep_task_id;
    CHECK_CODE(dominoHumanSubmitSleepTask(human_ptr, DOMINO_HUMAN_TASK_CHANNEL_SECONDARY_1, 2000U, &sleep_task_id));
    DominoTask* sleep_task_ptr = dominoGetTaskByID(sleep_task_id);
    CHECK_CODE(dominoHumanStartSleepTask(sleep_task_ptr, human_ptr, 2000U));
    const domino_runtime_ms_t partial_ms = 2000U + DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS * 2U + 123U;
    setFixtureTimeMs(partial_ms);
    CHECK_CODE(dominoHumanAdvanceSleepTask(sleep_task_ptr, human_ptr, partial_ms));
    TaskSnapshot sleep_snapshot;
    captureTask(sleep_task_id, &sleep_snapshot);
    CHECK(sleep_snapshot.task.status == DOMINO_TASK_STATUS_RUNNING);
    CHECK(sleep_snapshot.behaviors[0].active_elapsed_ms == partial_ms - 2000U);
    dominoEngineStop();
    dominoEngineDestroy();
    checkDestroyedStore();

    dominoEngineInit(config);
    checkTask(&sleep_snapshot, true);
    human_ptr = dominoGetHumanByID(1U);
    sleep_task_ptr = dominoGetTaskByID(sleep_task_id);
    DominoBehavior* sleep_behavior_ptr = dominoGetBehaviorByID(sleep_task_ptr->current_behavior_id);
    CHECK(human_ptr->data->active_task_id_list[DOMINO_HUMAN_TASK_CHANNEL_SECONDARY_1] == sleep_task_id);
    const domino_runtime_ms_t target_ms = sleep_behavior_ptr->planned_end_ms - sleep_behavior_ptr->planned_begin_ms;
    const domino_runtime_ms_t completed_ms = sleep_behavior_ptr->accounted_at_ms + target_ms - sleep_behavior_ptr->active_elapsed_ms;
    setFixtureTimeMs(completed_ms);
    CHECK_CODE(dominoHumanAdvanceSleepTask(sleep_task_ptr, human_ptr, completed_ms));
    CHECK(sleep_task_ptr->current_behavior_id == 0U && sleep_task_ptr->status == DOMINO_TASK_STATUS_SUCCEEDED);
    CHECK(sleep_behavior_ptr->status == DOMINO_BEHAVIOR_STATUS_DONE && sleep_behavior_ptr->active_elapsed_ms == target_ms);
    CHECK_CODE(dominoHumanAdvanceSleepTask(sleep_task_ptr, human_ptr, completed_ms));
    CHECK(sleep_task_ptr->execution_version == 2U);
    CHECK_CODE(dominoHumanStoreRemoveTask(sleep_task_id));
    dominoEngineStop();
    dominoEngineDestroy();
    checkDestroyedStore();

    dominoEngineInit(config);
    dominoGetHumanDataByID(1U)->energy = 20U;
    dominoEngineStart(nullptr);
    /* Start 后只走公开控制入口；Stop 回收所有线程后再检查实例资源，避免读取运行中的容器。 */
    dominoEngineStop();
    dominoEngineDestroy();
    checkDestroyedStore();
    DOMINO_PRINT_MESSAGE("human task store regression passed");
    return 0;
}
