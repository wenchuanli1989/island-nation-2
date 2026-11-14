#include "store.h"

#include <stdlib.h>
#include <string.h>

#include "../host/entry.h"
#include "domino_shared_common.h"
#include "state.h"

static size_t g_plan_count;
static DominoPlanCopyMetrics g_copy_metrics;
DominoPlanCopyMetrics dominoHumanStoreCopyMetrics(void) {
    return g_copy_metrics;
}
/** @brief 单写入者分配持久正式身份，已回收身份也永不复用。 */
static uint32_t allocateID(_Atomic uint32_t* counter) {
    uint32_t last = atomic_load(counter);
    if (last == UINT32_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "human entity ID exhausted");
    atomic_store(counter, last + 1U);
    return last + 1U;
}

KHASHL_MAP_INIT(KH_LOCAL, MotivationMap, motivationMap, uint32_t, uint32_t, kh_hash_uint32, kh_eq_generic)
DominoMotivationVec domino_all_motivation_list;
static MotivationMap* g_motivation_map;
DominoMotivation* dominoGetMotivationByID(uint32_t id) {
    khint_t index = motivationMap_get(g_motivation_map, id);
    return kh_exist(g_motivation_map, index) ? &kv_A(domino_all_motivation_list, kh_val(g_motivation_map, index)) : nullptr;
}
DOMINO_CODE dominoHumanStoreImportMotivation(const DominoMotivation* value) {
    if (kv_size(domino_all_motivation_list) >= DOMINO_MOTIVATION_POOL_MAX)
        return ERR_CAPACITY_EXCEEDED;
    int absent;
    khint_t index = motivationMap_put(g_motivation_map, value->motivation_id, &absent);
    if (absent < 0)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot index motivation");
    kh_val(g_motivation_map, index) = (uint32_t)kv_size(domino_all_motivation_list);
    kv_push(DominoMotivation, domino_all_motivation_list, *value);
    return CODE_OK;
}

KHASHL_MAP_INIT(KH_LOCAL, TaskMap, taskMap, uint32_t, uint32_t, kh_hash_uint32, kh_eq_generic)
DominoTaskVec domino_all_task_list;
static TaskMap* g_task_map;
DominoTask* dominoGetTaskByID(uint32_t id) {
    khint_t index = taskMap_get(g_task_map, id);
    return kh_exist(g_task_map, index) ? &kv_A(domino_all_task_list, kh_val(g_task_map, index)) : nullptr;
}
DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* value) {
    if (kv_size(domino_all_task_list) >= DOMINO_TASK_POOL_MAX)
        return ERR_CAPACITY_EXCEEDED;
    int absent;
    khint_t index = taskMap_put(g_task_map, value->task_id, &absent);
    if (absent < 0)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot index task");
    kh_val(g_task_map, index) = (uint32_t)kv_size(domino_all_task_list);
    kv_push(DominoTask, domino_all_task_list, *value);
    g_plan_count += value->plan_count;
    if (g_plan_count > DOMINO_BEHAVIOR_PLAN_POOL_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "plan pool full");
    return CODE_OK;
}
static void removeTask(uint32_t id) {
    khint_t index = taskMap_get(g_task_map, id);
    uint32_t offset = kh_val(g_task_map, index);
    taskMap_del(g_task_map, index);
    DominoTask last = kv_pop(domino_all_task_list);
    if (offset != kv_size(domino_all_task_list)) {
        kv_A(domino_all_task_list, offset) = last;
        kh_val(g_task_map, taskMap_get(g_task_map, last.task_id)) = offset;
    }
}

KHASHL_MAP_INIT(KH_LOCAL, BehaviorMap, behaviorMap, uint32_t, uint32_t, kh_hash_uint32, kh_eq_generic)
DominoBehaviorVec domino_all_behavior_list;
static BehaviorMap* g_behavior_map;
DominoBehavior* dominoGetBehaviorByID(uint32_t id) {
    khint_t index = behaviorMap_get(g_behavior_map, id);
    return kh_exist(g_behavior_map, index) ? &kv_A(domino_all_behavior_list, kh_val(g_behavior_map, index)) : nullptr;
}
DOMINO_CODE dominoHumanStoreImportBehavior(const DominoBehavior* value) {
    if (kv_size(domino_all_behavior_list) >= DOMINO_BEHAVIOR_POOL_MAX)
        return ERR_CAPACITY_EXCEEDED;
    int absent;
    khint_t index = behaviorMap_put(g_behavior_map, value->behavior_id, &absent);
    if (absent < 0)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot index behavior");
    kh_val(g_behavior_map, index) = (uint32_t)kv_size(domino_all_behavior_list);
    kv_push(DominoBehavior, domino_all_behavior_list, *value);
    return CODE_OK;
}
static void removeBehavior(uint32_t id) {
    khint_t index = behaviorMap_get(g_behavior_map, id);
    uint32_t offset = kh_val(g_behavior_map, index);
    behaviorMap_del(g_behavior_map, index);
    DominoBehavior last = kv_pop(domino_all_behavior_list);
    if (offset != kv_size(domino_all_behavior_list)) {
        kv_A(domino_all_behavior_list, offset) = last;
        kh_val(g_behavior_map, behaviorMap_get(g_behavior_map, last.behavior_id)) = offset;
    }
}
void dominoHumanStoreInit(void) {
    g_motivation_map = motivationMap_init();
    if (!g_motivation_map)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate motivation index");
    g_task_map = taskMap_init();
    if (!g_task_map)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate task index");
    g_behavior_map = behaviorMap_init();
    if (!g_behavior_map)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate behavior index");
}
void dominoHumanStoreDestroy(void) {
    for (size_t i = 0; i < kv_size(domino_all_task_list); ++i)
        free(kv_A(domino_all_task_list, i).plans);
    kv_destroy(domino_all_motivation_list);
    kv_init(domino_all_motivation_list);
    motivationMap_destroy(g_motivation_map);
    g_motivation_map = nullptr;
    kv_destroy(domino_all_task_list);
    kv_init(domino_all_task_list);
    taskMap_destroy(g_task_map);
    g_task_map = nullptr;
    kv_destroy(domino_all_behavior_list);
    kv_init(domino_all_behavior_list);
    behaviorMap_destroy(g_behavior_map);
    g_behavior_map = nullptr;
    g_plan_count = 0;
    g_copy_metrics = (DominoPlanCopyMetrics){0};
}
domino_motivation_id_t dominoHumanStoreAddMotivation(DominoHumanData* data, DominoMotivation value) {
    value.motivation_id = allocateID(&g_domino_global_increment_id.motivation_increment_id);
    value.owner_human_id = data->human_id;
    DOMINO_CODE code = dominoHumanStoreImportMotivation(&value);
    if (code != CODE_OK)
        DOMINO_EXIT_WITH_ERROR(code, "prechecked motivation insertion failed");
    data->motivation_ids[data->motivation_count++] = value.motivation_id;
    return value.motivation_id;
}
domino_task_id_t dominoHumanStoreAddTask(DominoHumanData* data, DominoTask value) {
    value.task_id = allocateID(&g_domino_global_increment_id.task_increment_id);
    value.owner_human_id = data->human_id;
    value.plan_version = 1;
    value.arrangement_version = 1;
    DOMINO_CODE code = dominoHumanStoreImportTask(&value);
    if (code != CODE_OK)
        DOMINO_EXIT_WITH_ERROR(code, "prechecked task insertion failed");
    data->task_id_list[data->task_count++] = value.task_id;
    return value.task_id;
}
domino_behavior_id_t dominoHumanStoreAddBehavior(DominoTask* task, DominoBehavior value) {
    value.behavior_id = allocateID(&g_domino_global_increment_id.behavior_increment_id);
    value.task_id = task->task_id;
    value.next_record_id = task->first_record_id;
    DOMINO_CODE code = dominoHumanStoreImportBehavior(&value);
    if (code != CODE_OK)
        DOMINO_EXIT_WITH_ERROR(code, "prechecked behavior insertion failed");
    task->first_record_id = value.behavior_id;
    ++task->record_count;
    return value.behavior_id;
}
static void advancePlanVersion(DominoTask* task) {
    if (task->plan_version == UINT64_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "plan version exhausted");
    ++task->plan_version;
}
void dominoHumanStoreAdvanceArrangement(DominoTask* task) {
    if (task->arrangement_version == UINT64_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "arrangement version exhausted");
    ++task->arrangement_version;
}
void dominoHumanStoreReplacePlans(DominoTask* task, const DominoBehaviorPlan* plans, uint16_t count) {
    if (task->plan_count == count && (!count || memcmp(task->plans, plans, (size_t)count * sizeof(*plans)) == 0))
        return;
    uint64_t start = dominoMonotonicTimeNs();
    g_plan_count -= task->plan_count;
    free(task->plans);
    task->plans = nullptr;
    task->plan_count = 0;
    DominoBehaviorPlan* copy = nullptr;
    if (count) {
        copy = malloc((size_t)count * sizeof(*copy));
        if (!copy)
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot copy behavior plan");
        memcpy(copy, plans, (size_t)count * sizeof(*copy));
    }
    g_plan_count += count;
    task->plans = copy;
    task->plan_count = count;
    advancePlanVersion(task);
    g_copy_metrics.bytes += (size_t)count * sizeof(*copy);
    dominoTimingRecord(&g_copy_metrics.timing, dominoMonotonicTimeNs() - start);
}
void dominoHumanStoreConsumePlan(DominoTask* task) {
    --task->plan_count;
    --g_plan_count;
    if (task->plan_count)
        memmove(task->plans, task->plans + 1, task->plan_count * sizeof(*task->plans));
    else {
        free(task->plans);
        task->plans = nullptr;
    }
    advancePlanVersion(task);
}
void dominoHumanStoreRecycleBehaviors(DominoTask* task) {
    domino_behavior_id_t* link = &task->first_record_id;
    while (*link) {
        DominoBehavior* behavior = dominoGetBehaviorByID(*link);
        if (behavior->status < DOMINO_BEHAVIOR_STATUS_SUCCEEDED) {
            link = &behavior->next_record_id;
            continue;
        }
        uint32_t id = behavior->behavior_id;
        uint32_t next = behavior->next_record_id;
        // 删除会移动记录；只用身份重新走链，不跨删除保留池指针。
        *link = next;
        removeBehavior(id);
        --task->record_count;
        link = &task->first_record_id;
    }
}
void dominoHumanStoreRecycleTask(DominoHumanData* data, uint8_t index) {
    DominoTask* task = dominoGetTaskByID(data->task_id_list[index]);
    dominoHumanStoreRecycleBehaviors(task);
    g_plan_count -= task->plan_count;
    free(task->plans);
    removeTask(task->task_id);
    --data->task_count;
    memmove(&data->task_id_list[index], &data->task_id_list[index + 1], (data->task_count - index) * sizeof(data->task_id_list[0]));
    data->task_id_list[data->task_count] = 0;
}
size_t dominoHumanStorePlanCount(void) {
    return g_plan_count;
}
