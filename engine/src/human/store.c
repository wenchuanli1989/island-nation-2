#include "store.h"

#include <stdlib.h>
#include <string.h>

#include "../host/entry.h"
#include "domino_shared_common.h"
#include "klib/khashl.h"
#include "rules.h"
#include "state.h"

KHASHL_MAP_INIT(KH_LOCAL, HumanMotivationIdMap, humanMotivationIdMap, domino_motivation_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, HumanTaskIdMap, humanTaskIdMap, domino_task_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, HumanBehaviorIdMap, humanBehaviorIdMap, domino_behavior_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

DominoMotivationVec g_domino_motivation_list;
DominoTaskVec g_domino_task_list;
DominoBehaviorVec g_domino_behavior_list;

static HumanMotivationIdMap* g_motivation_id_map;
static HumanTaskIdMap* g_task_id_map;
static HumanBehaviorIdMap* g_behavior_id_map;
static uint32_t g_plan_item_count;
static uint64_t g_last_plan_version;

void dominoHumanStoreInit(void) {
    g_motivation_id_map = humanMotivationIdMap_init();
    g_task_id_map = humanTaskIdMap_init();
    g_behavior_id_map = humanBehaviorIdMap_init();
    if (!g_motivation_id_map || !g_task_id_map || !g_behavior_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human store ID maps");
    }
}

void dominoHumanStoreDestroy(void) {
    for (size_t index = 0U; index < kv_size(g_domino_task_list); index++) {
        free(kv_A(g_domino_task_list, index).plan_item_list);
    }
    kv_destroy(g_domino_motivation_list);
    kv_init(g_domino_motivation_list);
    kv_destroy(g_domino_task_list);
    kv_init(g_domino_task_list);
    kv_destroy(g_domino_behavior_list);
    kv_init(g_domino_behavior_list);
    humanMotivationIdMap_destroy(g_motivation_id_map);
    humanTaskIdMap_destroy(g_task_id_map);
    humanBehaviorIdMap_destroy(g_behavior_id_map);
    g_motivation_id_map = nullptr;
    g_task_id_map = nullptr;
    g_behavior_id_map = nullptr;
    g_plan_item_count = 0U;
    g_last_plan_version = 0U;
}

DominoMotivation* dominoGetMotivationById(domino_motivation_id_t motivation_id) {
    const khint_t map_index = humanMotivationIdMap_get(g_motivation_id_map, motivation_id);
    return kh_exist(g_motivation_id_map, map_index) ? &kv_A(g_domino_motivation_list, kh_val(g_motivation_id_map, map_index)) : nullptr;
}

DominoTask* dominoGetTaskById(domino_task_id_t task_id) {
    const khint_t map_index = humanTaskIdMap_get(g_task_id_map, task_id);
    return kh_exist(g_task_id_map, map_index) ? &kv_A(g_domino_task_list, kh_val(g_task_id_map, map_index)) : nullptr;
}

DominoBehavior* dominoGetBehaviorById(domino_behavior_id_t behavior_id) {
    const khint_t map_index = humanBehaviorIdMap_get(g_behavior_id_map, behavior_id);
    return kh_exist(g_behavior_id_map, map_index) ? &kv_A(g_domino_behavior_list, kh_val(g_behavior_id_map, map_index)) : nullptr;
}

uint32_t dominoHumanStoreMotivationCount(void) {
    return (uint32_t)kv_size(g_domino_motivation_list);
}

uint32_t dominoHumanStoreTaskCount(void) {
    return (uint32_t)kv_size(g_domino_task_list);
}

uint32_t dominoHumanStoreBehaviorCount(void) {
    return (uint32_t)kv_size(g_domino_behavior_list);
}

uint32_t dominoHumanStorePlanItemCount(void) {
    return g_plan_item_count;
}

uint64_t dominoHumanStoreNextPlanVersion(void) {
    if (g_last_plan_version == UINT64_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "human plan version exhausted");
    }
    return ++g_last_plan_version;
}

/** @brief 计数器保存最后已分配 ID，加一后返回；耗尽致命退出，不回绕。只由执行器或串行阶段调用。 */
static uint32_t allocFormalId(_Atomic uint32_t* counter_ptr, const char* what_str) {
    const uint32_t last_id = atomic_load_explicit(counter_ptr, memory_order_relaxed);
    if (last_id == UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "%s ID exhausted", what_str);
    }
    atomic_store_explicit(counter_ptr, last_id + 1U, memory_order_relaxed);
    return last_id + 1U;
}

/** @brief 把 id → 池下标写入索引；分配失败致命退出。 */
#define STORE_PUT_ID(prefix, map, id, index_value)                                                      \
    do {                                                                                                \
        int absent_flag;                                                                                \
        const khint_t map_index = prefix##_put((map), (id), &absent_flag);                              \
        if (absent_flag < 0) {                                                                          \
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot insert human store ID index id=%u", (id)); \
        }                                                                                               \
        kh_val((map), map_index) = (index_value);                                                       \
    } while (0)

static void appendMotivation(const DominoMotivation* motivation_ptr) {
    STORE_PUT_ID(humanMotivationIdMap, g_motivation_id_map, motivation_ptr->motivation_id, (uint32_t)kv_size(g_domino_motivation_list));
    kv_push(DominoMotivation, g_domino_motivation_list, *motivation_ptr);
}

static void appendTask(const DominoTask* task_ptr) {
    STORE_PUT_ID(humanTaskIdMap, g_task_id_map, task_ptr->task_id, (uint32_t)kv_size(g_domino_task_list));
    kv_push(DominoTask, g_domino_task_list, *task_ptr);
}

static void appendBehavior(const DominoBehavior* behavior_ptr) {
    STORE_PUT_ID(humanBehaviorIdMap, g_behavior_id_map, behavior_ptr->behavior_id, (uint32_t)kv_size(g_domino_behavior_list));
    kv_push(DominoBehavior, g_domino_behavior_list, *behavior_ptr);
}

/** @brief 复制出恰好 count 项的独占计划块；count 为 0 返回空指针。 */
static DominoBehaviorPlanItem* copyPlanItems(const DominoBehaviorPlanItem* item_list, uint8_t count) {
    if (count == 0U) {
        return nullptr;
    }
    DominoBehaviorPlanItem* copy_ptr = malloc(sizeof(*copy_ptr) * count);
    if (!copy_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate behavior plan items count=%u", count);
    }
    memcpy(copy_ptr, item_list, sizeof(*copy_ptr) * count);
    return copy_ptr;
}

DOMINO_CODE dominoHumanStoreImportMotivation(const DominoMotivation* motivation_ptr) {
    if (kv_size(g_domino_motivation_list) >= DOMINO_HUMAN_MOTIVATION_POOL_MAX) {
        return ERR_CAPACITY_EXCEEDED;
    }
    appendMotivation(motivation_ptr);
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr, const DominoBehaviorPlanItem* plan_item_list, uint8_t plan_item_count) {
    if (kv_size(g_domino_task_list) >= DOMINO_HUMAN_TASK_POOL_MAX || plan_item_count > DOMINO_HUMAN_PLAN_ITEM_POOL_MAX - g_plan_item_count) {
        return ERR_CAPACITY_EXCEEDED;
    }
    DominoTask task = *task_ptr;
    task.plan_item_list = copyPlanItems(plan_item_list, plan_item_count);
    task.plan_item_count = plan_item_count;
    task.plan_version = 0U;
    task.behavior_record_count = 0U;
    g_plan_item_count += plan_item_count;
    appendTask(&task);
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreImportBehavior(const DominoBehavior* behavior_ptr) {
    if (kv_size(g_domino_behavior_list) >= DOMINO_HUMAN_BEHAVIOR_POOL_MAX) {
        return ERR_CAPACITY_EXCEEDED;
    }
    appendBehavior(behavior_ptr);
    return CODE_OK;
}

DominoMotivation* dominoHumanStoreAddMotivation(DominoHumanData* data_ptr, const DominoMotivation* init_ptr) {
    DominoMotivation motivation = *init_ptr;
    motivation.motivation_id = allocFormalId(&g_domino_id_counters.last_motivation_id, "motivation");
    motivation.owner_human_id = data_ptr->human_id;
    appendMotivation(&motivation);
    data_ptr->motivation_id_list[data_ptr->motivation_count++] = motivation.motivation_id;
    return &kv_A(g_domino_motivation_list, kv_size(g_domino_motivation_list) - 1U);
}

DominoTask* dominoHumanStoreAddTask(DominoHumanData* data_ptr, const DominoTask* init_ptr) {
    DominoTask task = *init_ptr;
    task.task_id = allocFormalId(&g_domino_id_counters.last_task_id, "task");
    task.owner_human_id = data_ptr->human_id;
    task.plan_item_list = nullptr;
    task.plan_item_count = 0U;
    task.plan_version = dominoHumanStoreNextPlanVersion();
    task.exec_status = DOMINO_TASK_EXEC_STATUS_NONE;
    task.started_at_ms = 0U;
    task.ended_at_ms = 0U;
    task.progress_ms = 0U;
    memset(task.current_behavior_id_list, 0, sizeof(task.current_behavior_id_list));
    task.last_behavior_id = 0U;
    task.last_step_seq = 0U;
    task.main_succeeded_count = 0U;
    task.main_failed_count = 0U;
    task.main_cancelled_count = 0U;
    task.behavior_record_count = 0U;
    appendTask(&task);
    data_ptr->task_id_list[data_ptr->task_count++] = task.task_id;
    return &kv_A(g_domino_task_list, kv_size(g_domino_task_list) - 1U);
}

void dominoHumanStoreReplacePlan(DominoTask* task_ptr, const DominoBehaviorPlanItem* item_list, uint8_t count) {
    DominoBehaviorPlanItem* copy_ptr = copyPlanItems(item_list, count);
    free(task_ptr->plan_item_list);
    g_plan_item_count = g_plan_item_count - task_ptr->plan_item_count + count;
    task_ptr->plan_item_list = copy_ptr;
    task_ptr->plan_item_count = count;
    task_ptr->plan_version = dominoHumanStoreNextPlanVersion();
}

DominoBehaviorPlanItem dominoHumanStoreConsumePlanItem(DominoTask* task_ptr, uint8_t index) {
    const DominoBehaviorPlanItem item = task_ptr->plan_item_list[index];
    task_ptr->plan_item_count--;
    g_plan_item_count--;
    if (task_ptr->plan_item_count == 0U) {
        free(task_ptr->plan_item_list);
        task_ptr->plan_item_list = nullptr;
    } else {
        memmove(&task_ptr->plan_item_list[index], &task_ptr->plan_item_list[index + 1U],
                sizeof(task_ptr->plan_item_list[0]) * (size_t)(task_ptr->plan_item_count - index));
    }
    task_ptr->plan_version = dominoHumanStoreNextPlanVersion();
    return item;
}

DominoBehavior* dominoHumanStoreAddBehavior(DominoTask* task_ptr, const DominoBehavior* init_ptr) {
    DominoBehavior behavior = *init_ptr;
    behavior.behavior_id = allocFormalId(&g_domino_id_counters.last_behavior_id, "behavior");
    behavior.task_id = task_ptr->task_id;
    appendBehavior(&behavior);
    task_ptr->behavior_record_count++;
    return &kv_A(g_domino_behavior_list, kv_size(g_domino_behavior_list) - 1U);
}

void dominoHumanStoreReclaimBehavior(DominoTask* task_ptr, domino_behavior_id_t behavior_id) {
    const khint_t map_index = humanBehaviorIdMap_get(g_behavior_id_map, behavior_id);
    const uint32_t pool_index = kh_val(g_behavior_id_map, map_index);
    humanBehaviorIdMap_del(g_behavior_id_map, map_index);
    const DominoBehavior last_behavior = kv_pop(g_domino_behavior_list);
    if (pool_index != kv_size(g_domino_behavior_list)) {
        kv_A(g_domino_behavior_list, pool_index) = last_behavior;
        kh_val(g_behavior_id_map, humanBehaviorIdMap_get(g_behavior_id_map, last_behavior.behavior_id)) = pool_index;
    }
    task_ptr->behavior_record_count--;
}

void dominoHumanStoreReclaimTask(DominoHumanData* data_ptr, uint8_t task_index) {
    const domino_task_id_t task_id = data_ptr->task_id_list[task_index];
    const khint_t task_map_index = humanTaskIdMap_get(g_task_id_map, task_id);
    const uint32_t task_pool_index = kh_val(g_task_id_map, task_map_index);
    DominoTask* task_ptr = &kv_A(g_domino_task_list, task_pool_index);
    // 回收行为只改变行为池，task_ptr 仍有效。
    if (task_ptr->last_behavior_id != 0U) {
        dominoHumanStoreReclaimBehavior(task_ptr, task_ptr->last_behavior_id);
        task_ptr->last_behavior_id = 0U;
    }
    free(task_ptr->plan_item_list);
    g_plan_item_count -= task_ptr->plan_item_count;
    data_ptr->task_count--;
    memmove(&data_ptr->task_id_list[task_index], &data_ptr->task_id_list[task_index + 1U],
            (size_t)(data_ptr->task_count - task_index) * sizeof(data_ptr->task_id_list[0]));
    data_ptr->task_id_list[data_ptr->task_count] = 0U;
    humanTaskIdMap_del(g_task_id_map, task_map_index);
    const DominoTask last_task = kv_pop(g_domino_task_list);
    if (task_pool_index != kv_size(g_domino_task_list)) {
        kv_A(g_domino_task_list, task_pool_index) = last_task;
        kh_val(g_task_id_map, humanTaskIdMap_get(g_task_id_map, last_task.task_id)) = task_pool_index;
    }
}

void dominoHumanStoreReclaimMotivation(DominoHumanData* data_ptr, uint8_t motivation_index) {
    const domino_motivation_id_t motivation_id = data_ptr->motivation_id_list[motivation_index];
    data_ptr->motivation_count--;
    memmove(&data_ptr->motivation_id_list[motivation_index], &data_ptr->motivation_id_list[motivation_index + 1U],
            (size_t)(data_ptr->motivation_count - motivation_index) * sizeof(data_ptr->motivation_id_list[0]));
    data_ptr->motivation_id_list[data_ptr->motivation_count] = 0U;
    const khint_t map_index = humanMotivationIdMap_get(g_motivation_id_map, motivation_id);
    const uint32_t pool_index = kh_val(g_motivation_id_map, map_index);
    humanMotivationIdMap_del(g_motivation_id_map, map_index);
    const DominoMotivation last_motivation = kv_pop(g_domino_motivation_list);
    if (pool_index != kv_size(g_domino_motivation_list)) {
        kv_A(g_domino_motivation_list, pool_index) = last_motivation;
        kh_val(g_motivation_id_map, humanMotivationIdMap_get(g_motivation_id_map, last_motivation.motivation_id)) = pool_index;
    }
}

void dominoHumanTaskQueueInsert(DominoHumanData* data_ptr, uint8_t index, domino_task_id_t task_id) {
    domino_task_id_t* queue_ptr = data_ptr->task_queue_list;
    const uint8_t count = data_ptr->task_queue_count;
    memmove(&queue_ptr[index + 1U], &queue_ptr[index], (size_t)(count - index) * sizeof(queue_ptr[0]));
    queue_ptr[index] = task_id;
    data_ptr->task_queue_count = (uint8_t)(count + 1U);
}

bool dominoHumanTaskQueueRemove(DominoHumanData* data_ptr, domino_task_id_t task_id) {
    domino_task_id_t* queue_ptr = data_ptr->task_queue_list;
    const uint8_t count = data_ptr->task_queue_count;
    for (uint8_t index = 0U; index < count; index++) {
        if (queue_ptr[index] == task_id) {
            memmove(&queue_ptr[index], &queue_ptr[index + 1U], (size_t)(count - index - 1U) * sizeof(queue_ptr[0]));
            queue_ptr[count - 1U] = 0U;
            data_ptr->task_queue_count = (uint8_t)(count - 1U);
            return true;
        }
    }
    return false;
}
