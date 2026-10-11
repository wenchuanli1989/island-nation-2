#include "store.h"

#include <stdlib.h>
#include <string.h>

#include "../host/entry.h"
#include "../social/entry.h"
#include "entry.h"
#include "klib/khashl.h"
#include "task/entry.h"

KHASHL_MAP_INIT(KH_LOCAL, HumanTaskIDMap, humanTaskIDMap, domino_task_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

DominoTaskVec domino_all_task_list;

static HumanTaskIDMap* g_task_id_map;

/* khashl 使用 32 位桶下标，最多 2^31 个桶，默认最大负载为 75%。 */
static const size_t HUMAN_STORE_COUNT_MAX = ((size_t)1U << 31U) - ((size_t)1U << 29U);

static bool isTaskTerminal(const DominoTask* task_ptr) {
    return task_ptr->status >= DOMINO_TASK_STATUS_SUCCEEDED;
}

static bool isBehaviorTerminal(const DominoBehavior* behavior_ptr) {
    return behavior_ptr->progress.status >= DOMINO_BEHAVIOR_STATUS_DONE;
}

/** @brief 人物身份及清单容量已校验；检查需求范围和任务 ID 去重。 */
static DOMINO_CODE validateHumanData(const DominoHumanData* data_ptr) {
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        for (uint8_t previous_index = 0U; previous_index < index; previous_index++) {
            if (data_ptr->task_id_list[previous_index] == data_ptr->task_id_list[index]) {
                return ERR_INVALID_DATA;
            }
        }
    }
    const uint8_t needs[] = {data_ptr->energy, data_ptr->food, data_ptr->water, data_ptr->hygiene, data_ptr->recreation, data_ptr->social};
    for (size_t index = 0U; index < sizeof(needs) / sizeof(needs[0]); index++) {
        if (needs[index] > DOMINO_HUMAN_NEED_MAX) {
            return ERR_INVALID_DATA;
        }
    }
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreInit(void) {
    if (g_task_id_map) {
        return ERR_ALREADY_INITIALIZED;
    }
    g_task_id_map = humanTaskIDMap_init();
    if (!g_task_id_map) {
        dominoHumanStoreExit();
        return ERR_MEMORY_ALLOC;
    }
    return CODE_OK;
}

void dominoHumanStoreExit(void) {
    for (size_t index = 0U; index < kv_size(domino_all_task_list); index++) {
        free(kv_A(domino_all_task_list, index).behaviors_ptr);
    }
    kv_destroy(domino_all_task_list);
    kv_init(domino_all_task_list);
    humanTaskIDMap_destroy(g_task_id_map);
    g_task_id_map = nullptr;
}

DominoTask* dominoGetTaskByID(domino_task_id_t task_id) {
    if (!g_task_id_map) {
        return nullptr;
    }
    khint_t map_index = humanTaskIDMap_get(g_task_id_map, task_id);
    return kh_exist(g_task_id_map, map_index) ? &kv_A(domino_all_task_list, kh_val(g_task_id_map, map_index)) : nullptr;
}

DominoBehavior* dominoGetBehaviorByTaskIDAndSequenceIndex(domino_task_id_t task_id, uint8_t sequence_index) {
    DominoTask* task_ptr = dominoGetTaskByID(task_id);
    if (!task_ptr || sequence_index >= task_ptr->behavior_count) {
        return nullptr;
    }
    return &task_ptr->behaviors_ptr[sequence_index];
}

DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr) {
    if (!g_task_id_map) {
        return ERR_NOT_INITIALIZED;
    }
    if (!task_ptr || !task_ptr->task_id || !task_ptr->owner_human_id) {
        return ERR_INVALID_DATA;
    }
    if (!task_ptr->behaviors_ptr) {
        return ERR_NULL_POINTER;
    }
    if (!task_ptr->behavior_count) {
        return ERR_INVALID_DATA;
    }
    if (dominoGetTaskByID(task_ptr->task_id)) {
        return ERR_ALREADY_EXISTS;
    }
    DominoTask task = *task_ptr;
    size_t count = kv_size(domino_all_task_list);
    if (count >= HUMAN_STORE_COUNT_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    int absent_flag;
    khint_t map_index = humanTaskIDMap_put(g_task_id_map, task.task_id, &absent_flag);
    if (absent_flag < 0) {
        return ERR_MEMORY_ALLOC;
    }
    task.execution_version = 1U;
    kh_val(g_task_id_map, map_index) = (uint32_t)count;
    kv_push(DominoTask, domino_all_task_list, task);
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreAddTask(const DominoTask* draft_ptr, domino_task_id_t* task_id_ptr) {
    if (!g_task_id_map) {
        return ERR_NOT_INITIALIZED;
    }
    if (!draft_ptr || !task_id_ptr) {
        return ERR_NULL_POINTER;
    }
    if (draft_ptr->task_id || draft_ptr->status != DOMINO_TASK_STATUS_PLANNED || draft_ptr->parent_task_id || draft_ptr->active_child_task_id ||
        draft_ptr->pending_child_task_id) {
        return ERR_INVALID_PARAM;
    }
    DOMINO_CODE code = dominoHumanValidateTaskRecord(draft_ptr);
    if (code != CODE_OK) {
        return code;
    }
    DominoHumanData* data_ptr = dominoGetHumanDataByID(draft_ptr->owner_human_id);
    if (!data_ptr) {
        return ERR_NOT_FOUND;
    }
    if (data_ptr->schedule_revision == UINT32_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    if (data_ptr->task_count >= DOMINO_HUMAN_TASK_COUNT_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    for (uint8_t index = 0U; index < draft_ptr->behavior_count; index++) {
        if (draft_ptr->behaviors_ptr[index].target_building_id != 0 && !dominoGetBuildingByID(draft_ptr->behaviors_ptr[index].target_building_id)) {
            return ERR_INVALID_DATA;
        }
    }
    DominoTask task = *draft_ptr;
    /* 任务池容量小于非零 ID 空间；单写线程按现有索引跳过占用号，无需另建回收表。 */
    do {
        DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.task_increment_id, task.task_id);
    } while (dominoGetTaskByID(task.task_id));
    code = dominoHumanStoreImportTask(&task);
    if (code != CODE_OK) {
        return code;
    }
    /* 导入成功后不再存在可失败步骤；人物数组只记录提交顺序，不随任务计划时间排序。 */
    data_ptr->task_id_list[data_ptr->task_count++] = task.task_id;
    data_ptr->schedule_revision++;
    *task_id_ptr = task.task_id;
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreRemoveTask(domino_task_id_t task_id) {
    const DominoTask* task_ptr = dominoGetTaskByID(task_id);
    if (!task_ptr) {
        return ERR_NOT_FOUND;
    }
    const DominoTask task = *task_ptr;
    if (!isTaskTerminal(&task) || task.parent_task_id || task.active_child_task_id || task.pending_child_task_id) {
        return ERR_GAME_STATE_INVALID;
    }
    DominoHumanData* data_ptr = dominoGetHumanDataByID(task.owner_human_id);
    if (!data_ptr) {
        return ERR_INVALID_DATA;
    }
    if (data_ptr->schedule_revision == UINT32_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    uint8_t task_index = data_ptr->task_count;
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        const DominoTask* other_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (other_ptr->task_id == task_id) {
            task_index = index;
        } else if (other_ptr->parent_task_id == task_id || other_ptr->active_child_task_id == task_id ||
                   other_ptr->pending_child_task_id == task_id || other_ptr->cancel_target_task_id == task_id) {
            return ERR_GAME_STATE_INVALID;
        }
    }
    if (task_index == data_ptr->task_count) {
        return ERR_INVALID_DATA;
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
    free(task.behaviors_ptr);
    khint_t map_index = humanTaskIDMap_get(g_task_id_map, task_id);
    uint32_t index = kh_val(g_task_id_map, map_index);
    humanTaskIDMap_del(g_task_id_map, map_index);
    size_t last_index = --domino_all_task_list.n;
    if (index != last_index) {
        domino_all_task_list.a[index] = domino_all_task_list.a[last_index];
        map_index = humanTaskIDMap_get(g_task_id_map, domino_all_task_list.a[index].task_id);
        kh_val(g_task_id_map, map_index) = index;
    }
    return CODE_OK;
}

/** @brief 本任务已通过局部校验；引用对象只读取标量字段，完整遍历时各自校验。 */
static DOMINO_CODE validateTaskRelations(const DominoTask* task_ptr, const DominoHumanData* data_ptr) {
    uint8_t channel = task_ptr->channel_index;
    if (task_ptr->status == DOMINO_TASK_STATUS_RUNNING && data_ptr->active_task_id_list[channel] != task_ptr->task_id) {
        return ERR_INVALID_DATA;
    }
    if ((task_ptr->status == DOMINO_TASK_STATUS_SUSPENDED) != (task_ptr->active_child_task_id != 0U)) {
        return ERR_INVALID_DATA;
    }
    if (task_ptr->parent_task_id) {
        const DominoTask* parent_ptr = dominoGetTaskByID(task_ptr->parent_task_id);
        if (!parent_ptr || parent_ptr->owner_human_id != task_ptr->owner_human_id || parent_ptr->active_child_task_id != task_ptr->task_id ||
            parent_ptr->channel_index != channel || parent_ptr->status != DOMINO_TASK_STATUS_SUSPENDED ||
            (task_ptr->status != DOMINO_TASK_STATUS_RUNNING && task_ptr->status != DOMINO_TASK_STATUS_SUSPENDED)) {
            return ERR_INVALID_DATA;
        }
    }
    if (task_ptr->active_child_task_id) {
        const DominoTask* child_ptr = dominoGetTaskByID(task_ptr->active_child_task_id);
        if (!child_ptr || child_ptr->parent_task_id != task_ptr->task_id) {
            return ERR_INVALID_DATA;
        }
    }
    if (task_ptr->pending_child_task_id) {
        const DominoTask* child_ptr = dominoGetTaskByID(task_ptr->pending_child_task_id);
        if (!child_ptr || child_ptr->owner_human_id != task_ptr->owner_human_id || child_ptr->status != DOMINO_TASK_STATUS_PLANNED ||
            child_ptr->parent_task_id || child_ptr->active_child_task_id || child_ptr->channel_index != channel) {
            return ERR_INVALID_DATA;
        }
    }
    uint8_t depth = 1U;
    const DominoTask* ancestor_ptr = task_ptr;
    bool cancel_target_is_ancestor = false;  // CHAIN 只匹配父级或更高祖先；取消自身使用 TASK。
    while (ancestor_ptr->parent_task_id) {
        if (++depth > DOMINO_HUMAN_TASK_CHAIN_DEPTH_MAX) {
            return ERR_INVALID_DATA;
        }
        ancestor_ptr = dominoGetTaskByID(ancestor_ptr->parent_task_id);
        if (!ancestor_ptr) {
            return ERR_INVALID_DATA;
        }
        cancel_target_is_ancestor |= ancestor_ptr->task_id == task_ptr->cancel_target_task_id;
    }
    if (task_ptr->pending_child_task_id && depth == DOMINO_HUMAN_TASK_CHAIN_DEPTH_MAX) {
        return ERR_INVALID_DATA;
    }
    if (task_ptr->cancel_target_task_id) {
        /* 本任务为 RUNNING；完整父链校验保证祖先存在、同属本人且为 SUSPENDED。 */
        if ((task_ptr->cancel_scope == DOMINO_TASK_CANCEL_TASK && task_ptr->cancel_target_task_id != task_ptr->task_id) ||
            (task_ptr->cancel_scope == DOMINO_TASK_CANCEL_CHAIN && !cancel_target_is_ancestor)) {
            return ERR_INVALID_DATA;
        }
    }
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreValidate(domino_runtime_ms_t now_ms) {
    if (!g_task_id_map) {
        return ERR_NOT_INITIALIZED;
    }
    size_t listed_task_count = 0U;
    for (size_t human_index = 0U; human_index < kv_size(domino_all_human_data_list); human_index++) {
        const DominoHumanData* data_ptr = &kv_A(domino_all_human_data_list, human_index);
        DOMINO_CODE code = validateHumanData(data_ptr);
        if (code != CODE_OK || data_ptr->needs_updated_at_ms > now_ms) {
            return ERR_INVALID_DATA;
        }
        listed_task_count += data_ptr->task_count;
        for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
            const domino_task_id_t task_id = data_ptr->task_id_list[index];
            const DominoTask* task_ptr = dominoGetTaskByID(task_id);
            if (!task_ptr || task_ptr->task_id != task_id || task_ptr->owner_human_id != data_ptr->human_id) {
                return ERR_INVALID_DATA;
            }
            code = dominoHumanValidateTaskRecord(task_ptr);
            if (code != CODE_OK) {
                return code;
            }
            if ((task_ptr->started_flag && task_ptr->started_at_ms > now_ms) || (isTaskTerminal(task_ptr) && task_ptr->ended_at_ms > now_ms)) {
                return ERR_INVALID_DATA;
            }
            for (uint8_t behavior_index = 0U; behavior_index < task_ptr->behavior_count; behavior_index++) {
                const DominoBehavior* behavior_ptr = &task_ptr->behaviors_ptr[behavior_index];
                if ((behavior_ptr->target_building_id != 0 && !dominoGetBuildingByID(behavior_ptr->target_building_id)) ||
                    (behavior_ptr->progress.started_flag && behavior_ptr->progress.accounted_at_ms > now_ms) ||
                    (isBehaviorTerminal(behavior_ptr) && behavior_ptr->progress.ended_at_ms > now_ms)) {
                    return ERR_INVALID_DATA;
                }
            }
            code = validateTaskRelations(task_ptr, data_ptr);
            if (code != CODE_OK) {
                return code;
            }
        }
        for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
            domino_task_id_t task_id = data_ptr->active_task_id_list[channel];
            if (!task_id) {
                continue;
            }
            const DominoTask* task_ptr = dominoGetTaskByID(task_id);
            if (!task_ptr || task_ptr->owner_human_id != data_ptr->human_id || task_ptr->status != DOMINO_TASK_STATUS_RUNNING ||
                task_ptr->channel_index != channel) {
                return ERR_INVALID_DATA;
            }
        }
    }
    /* 各人的清单按 owner 隔离且各自去重，计数相等即保证全池任务恰好归属现存人物。 */
    return listed_task_count == kv_size(domino_all_task_list) ? CODE_OK : ERR_INVALID_DATA;
}

void dominoHumanStoreResetRuntime(void) {
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        DominoHumanData* data_ptr = &kv_A(domino_all_human_data_list, index);
        human_ptr->active_plan_request_id = 0U;
        human_ptr->wakeup_timer_id = 0U;
        human_ptr->wakeup_version = 0U;
        human_ptr->plan_submit_pending_flag = false;
        human_ptr->execution_queued_flag = false;
        human_ptr->wakeup_registration_pending_flag = false;
        data_ptr->pending_plan_reasons = 0U;
        data_ptr->pending_due_ms = 0U;
    }
}
