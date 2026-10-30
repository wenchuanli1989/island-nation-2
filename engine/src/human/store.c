#include "store.h"

#include <string.h>

#include "../host/entry.h"
#include "domino_shared_common.h"
#include "entry.h"
#include "klib/khashl.h"

KHASHL_MAP_INIT(KH_LOCAL, HumanTaskIDMap, humanTaskIDMap, domino_task_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, HumanBehaviorIDMap, humanBehaviorIDMap, domino_behavior_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

DominoTaskVec domino_all_task_list;
DominoBehaviorVec domino_all_behavior_list;

static HumanTaskIDMap* g_task_id_map;
static HumanBehaviorIDMap* g_behavior_id_map;

/* khashl 使用 32 位桶下标，最多 2^31 个桶，默认最大负载为 75%。 */
static const size_t HUMAN_STORE_COUNT_MAX = ((size_t)1U << 31U) - ((size_t)1U << 29U);

static bool isTaskTerminal(const DominoTask* task_ptr) {
    return task_ptr->status >= DOMINO_TASK_STATUS_SUCCEEDED;
}

void dominoHumanStoreInit(void) {
    g_task_id_map = humanTaskIDMap_init();
    if (!g_task_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "人物任务 ID 索引分配失败");
    }
    g_behavior_id_map = humanBehaviorIDMap_init();
    if (!g_behavior_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "人物行为 ID 索引分配失败");
    }
}

void dominoHumanStoreDestroy(void) {
    kv_destroy(domino_all_task_list);
    kv_init(domino_all_task_list);
    kv_destroy(domino_all_behavior_list);
    kv_init(domino_all_behavior_list);
    humanTaskIDMap_destroy(g_task_id_map);
    g_task_id_map = nullptr;
    humanBehaviorIDMap_destroy(g_behavior_id_map);
    g_behavior_id_map = nullptr;
}

DominoTask* dominoGetTaskByID(domino_task_id_t task_id) {
    khint_t map_index = humanTaskIDMap_get(g_task_id_map, task_id);
    return kh_exist(g_task_id_map, map_index) ? &kv_A(domino_all_task_list, kh_val(g_task_id_map, map_index)) : nullptr;
}

DominoBehavior* dominoGetBehaviorByID(domino_behavior_id_t behavior_id) {
    const khint_t map_index = humanBehaviorIDMap_get(g_behavior_id_map, behavior_id);
    return kh_exist(g_behavior_id_map, map_index) ? &kv_A(domino_all_behavior_list, kh_val(g_behavior_id_map, map_index)) : nullptr;
}

/** @brief store 已初始化，可信加载或提交入口保证有效任务及未占用 ID；按值入池，扩容不会使输入失效。 */
static void appendTask(DominoTask task) {
    const size_t count = kv_size(domino_all_task_list);
    int absent_flag;
    khint_t map_index = humanTaskIDMap_put(g_task_id_map, task.task_id, &absent_flag);
    if (absent_flag < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "插入人物任务 ID 索引时分配失败: task_id=%u, count=%zu", task.task_id, count);
    }
    task.execution_version = 1U;
    kh_val(g_task_id_map, map_index) = (uint32_t)count;
    kv_push(DominoTask, domino_all_task_list, task);
}

/** @brief 容量和身份由导入或提交入口保证；行为按值入池，避免保留可能失效的借用地址。 */
static void appendBehavior(DominoBehavior behavior) {
    const size_t count = kv_size(domino_all_behavior_list);
    int absent_flag;
    const khint_t map_index = humanBehaviorIDMap_put(g_behavior_id_map, behavior.behavior_id, &absent_flag);
    if (absent_flag < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "插入人物行为 ID 索引时分配失败: behavior_id=%u, count=%zu", behavior.behavior_id, count);
    }
    kh_val(g_behavior_id_map, map_index) = (uint32_t)count;
    kv_push(DominoBehavior, domino_all_behavior_list, behavior);
}

DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr) {
    if (kv_size(domino_all_task_list) >= HUMAN_STORE_COUNT_MAX) {
        return ERR_CAPACITY_EXCEEDED;
    }
    appendTask(*task_ptr);
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreImportBehavior(const DominoBehavior* behavior_ptr) {
    if (kv_size(domino_all_behavior_list) >= HUMAN_STORE_COUNT_MAX) {
        return ERR_CAPACITY_EXCEEDED;
    }
    appendBehavior(*behavior_ptr);
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreAddValidatedTask(const DominoTask* draft_ptr, const DominoBehavior* behaviors_ptr, DominoHumanData* data_ptr,
                                             domino_task_id_t* task_id_ptr) {
    if (data_ptr->schedule_revision == UINT32_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    if (data_ptr->task_count >= DOMINO_HUMAN_TASK_COUNT_MAX || kv_size(domino_all_task_list) >= HUMAN_STORE_COUNT_MAX ||
        draft_ptr->behavior_count > HUMAN_STORE_COUNT_MAX - kv_size(domino_all_behavior_list)) {
        return ERR_CAPACITY_EXCEEDED;
    }
    DominoTask task = *draft_ptr;
    /* 任务池容量小于非零 ID 空间；单写线程按现有索引跳过占用号，无需另建回收表。 */
    do {
        DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.task_increment_id, task.task_id);
    } while (dominoGetTaskByID(task.task_id));
    /* 容量已统一预检，后续只可能发生直接致命退出的资源失败；不建立部分提交回滚。 */
    for (uint32_t index = 0U; index < task.behavior_count; index++) {
        DominoBehavior behavior = behaviors_ptr[index];
        do {
            DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.behavior_increment_id, behavior.behavior_id);
        } while (dominoGetBehaviorByID(behavior.behavior_id));
        appendBehavior(behavior);
        task.behavior_id_list[index] = behavior.behavior_id;
    }
    task.current_behavior_id = task.behavior_id_list[0];
    appendTask(task);
    /* 导入成功后不再存在可失败步骤；人物数组只记录提交顺序，不随任务计划时间排序。 */
    data_ptr->task_id_list[data_ptr->task_count++] = task.task_id;
    data_ptr->schedule_revision++;
    *task_id_ptr = task.task_id;
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreRemoveTask(domino_task_id_t task_id) {
    const khint_t task_map_index = humanTaskIDMap_get(g_task_id_map, task_id);
    if (!kh_exist(g_task_id_map, task_map_index)) {
        return ERR_NOT_FOUND;
    }
    const uint32_t task_pool_index = kh_val(g_task_id_map, task_map_index);
    const DominoTask task = kv_A(domino_all_task_list, task_pool_index);
    if (!isTaskTerminal(&task)) {
        return ERR_GAME_STATE_INVALID;
    }
    DominoHumanData* data_ptr = dominoGetHumanDataByID(task.owner_human_id);
    if (!data_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "待回收任务所属人物不存在: task_id=%u, owner_human_id=%u", task_id, task.owner_human_id);
    }
    if (data_ptr->schedule_revision == UINT32_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    uint8_t task_index = data_ptr->task_count;
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        if (data_ptr->task_id_list[index] == task_id) {
            task_index = index;
            break;
        }
    }
    if (task_index == data_ptr->task_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "待回收任务未登记在所属人物清单中: task_id=%u, human_id=%u", task_id, data_ptr->human_id);
    }
    /* 有效区间保持提交顺序；末尾补零，不让任务池的交换删除改变人物清单顺序。 */
    data_ptr->task_count--;
    memmove(&data_ptr->task_id_list[task_index], &data_ptr->task_id_list[task_index + 1U],
            (data_ptr->task_count - task_index) * sizeof(data_ptr->task_id_list[0]));
    data_ptr->task_id_list[data_ptr->task_count] = 0U;
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        if (data_ptr->active_task_id_list[channel] == task_id) {
            data_ptr->active_task_id_list[channel] = 0U;
        }
    }
    data_ptr->schedule_revision++;
    /* 前述只读查询不改变哈希槽位；删除后被换入任务的槽位必须重新查询。 */
    humanTaskIDMap_del(g_task_id_map, task_map_index);
    const DominoTask last_task = kv_pop(domino_all_task_list);
    if (task_pool_index != kv_size(domino_all_task_list)) {
        kv_A(domino_all_task_list, task_pool_index) = last_task;
        const khint_t moved_map_index = humanTaskIDMap_get(g_task_id_map, last_task.task_id);
        kh_val(g_task_id_map, moved_map_index) = task_pool_index;
    }
    /* 每次删除可能移动其他行为，后续按 ID 重新定位，不能复用旧数组或哈希下标。 */
    for (uint32_t index = 0U; index < task.behavior_count; index++) {
        const khint_t behavior_map_index = humanBehaviorIDMap_get(g_behavior_id_map, task.behavior_id_list[index]);
        const uint32_t behavior_pool_index = kh_val(g_behavior_id_map, behavior_map_index);
        humanBehaviorIDMap_del(g_behavior_id_map, behavior_map_index);
        const DominoBehavior last_behavior = kv_pop(domino_all_behavior_list);
        if (behavior_pool_index != kv_size(domino_all_behavior_list)) {
            kv_A(domino_all_behavior_list, behavior_pool_index) = last_behavior;
            const khint_t moved_map_index = humanBehaviorIDMap_get(g_behavior_id_map, last_behavior.behavior_id);
            kh_val(g_behavior_id_map, moved_map_index) = behavior_pool_index;
        }
    }
    return CODE_OK;
}

void dominoHumanStoreResetRuntime(void) {
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        DominoHumanData* data_ptr = &kv_A(domino_all_human_data_list, index);
        human_ptr->active_plan_request_id = 0U;
        human_ptr->wakeup_game_timer_id = 0U;
        human_ptr->wakeup_due_ms = 0U;
        human_ptr->plan_submit_pending_flag = false;
        human_ptr->execution_queued_flag = false;
        data_ptr->pending_plan_reasons = 0U;
    }
}
