#include "store.h"

#include <stdlib.h>
#include <string.h>

#include "../host/entry.h"
#include "../social/entry.h"
#include "domino_shared_common.h"
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
static void validateHumanData(const DominoHumanData* data_ptr) {
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        for (uint8_t previous_index = 0U; previous_index < index; previous_index++) {
            if (data_ptr->task_id_list[previous_index] == data_ptr->task_id_list[index]) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "人物任务清单包含重复 ID: human_id=%u, task_id=%u, index=%u", data_ptr->human_id,
                                       data_ptr->task_id_list[index], index);
            }
        }
    }
    const uint8_t needs[] = {data_ptr->energy, data_ptr->food, data_ptr->water, data_ptr->hygiene, data_ptr->recreation, data_ptr->social};
    for (size_t index = 0U; index < sizeof(needs) / sizeof(needs[0]); index++) {
        if (needs[index] > DOMINO_HUMAN_NEED_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "人物需求值超出范围: human_id=%u, need_index=%zu, value=%u", data_ptr->human_id, index,
                                   needs[index]);
        }
    }
}

void dominoHumanStoreInit(void) {
    g_task_id_map = humanTaskIDMap_init();
    if (!g_task_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "人物任务 ID 索引分配失败");
    }
}

void dominoHumanStoreDestroy(void) {
    for (size_t index = 0U; index < kv_size(domino_all_task_list); index++) {
        free(kv_A(domino_all_task_list, index).behaviors_ptr);
    }
    kv_destroy(domino_all_task_list);
    kv_init(domino_all_task_list);
    humanTaskIDMap_destroy(g_task_id_map);
    g_task_id_map = nullptr;
}

DominoTask* dominoGetTaskByID(domino_task_id_t task_id) {
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

/** @brief store 已初始化，可信加载或提交入口保证有效任务及未占用 ID；按值入池，扩容不会使输入失效。 */
static DOMINO_CODE appendTask(DominoTask task) {
    size_t count = kv_size(domino_all_task_list);
    if (count >= HUMAN_STORE_COUNT_MAX) {
        return ERR_CAPACITY_EXCEEDED;
    }
    int absent_flag;
    khint_t map_index = humanTaskIDMap_put(g_task_id_map, task.task_id, &absent_flag);
    if (absent_flag < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "插入人物任务 ID 索引时分配失败: task_id=%u, count=%zu", task.task_id, count);
    }
    task.execution_version = 1U;
    kh_val(g_task_id_map, map_index) = (uint32_t)count;
    kv_push(DominoTask, domino_all_task_list, task);
    return CODE_OK;
}

DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr) {
    return appendTask(*task_ptr);
}

DOMINO_CODE dominoHumanStoreAddValidatedTask(const DominoTask* draft_ptr, DominoHumanData* data_ptr, domino_task_id_t* task_id_ptr) {
    if (data_ptr->schedule_revision == UINT32_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    if (data_ptr->task_count >= DOMINO_HUMAN_TASK_COUNT_MAX) {
        return ERR_CAPACITY_EXCEEDED;
    }
    DominoTask task = *draft_ptr;
    /* 任务池容量小于非零 ID 空间；单写线程按现有索引跳过占用号，无需另建回收表。 */
    do {
        DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.task_increment_id, task.task_id);
    } while (dominoGetTaskByID(task.task_id));
    DOMINO_CODE code = appendTask(task);
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
    const khint_t task_map_index = humanTaskIDMap_get(g_task_id_map, task_id);
    if (!kh_exist(g_task_id_map, task_map_index)) {
        return ERR_NOT_FOUND;
    }
    const uint32_t task_pool_index = kh_val(g_task_id_map, task_map_index);
    const DominoTask task = kv_A(domino_all_task_list, task_pool_index);
    if (!isTaskTerminal(&task) || task.parent_task_id || task.active_child_task_id || task.pending_child_task_id) {
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
            continue;
        }
        const DominoTask* other_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (!other_ptr) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "人物任务清单引用不存在的任务: human_id=%u, task_id=%u", data_ptr->human_id,
                                   data_ptr->task_id_list[index]);
        }
        if (other_ptr->parent_task_id == task_id || other_ptr->active_child_task_id == task_id || other_ptr->pending_child_task_id == task_id ||
            other_ptr->cancel_target_task_id == task_id) {
            return ERR_GAME_STATE_INVALID;
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
    free(task.behaviors_ptr);
    return CODE_OK;
}

/** @brief 本任务已通过局部校验；引用对象只读取标量字段，完整遍历时各自校验。 */
static void validateTaskRelations(const DominoTask* task_ptr, const DominoHumanData* data_ptr) {
    uint8_t channel = task_ptr->channel_index;
    if (task_ptr->status == DOMINO_TASK_STATUS_RUNNING && data_ptr->active_task_id_list[channel] != task_ptr->task_id) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "运行中任务与人物活动槽不匹配: task_id=%u, human_id=%u, channel=%u, active_task_id=%u",
                               task_ptr->task_id, data_ptr->human_id, channel, data_ptr->active_task_id_list[channel]);
    }
    if ((task_ptr->status == DOMINO_TASK_STATUS_SUSPENDED) != (task_ptr->active_child_task_id != 0U)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "任务挂起状态与活动子任务不匹配: task_id=%u, status=%u, active_child=%u", task_ptr->task_id,
                               task_ptr->status, task_ptr->active_child_task_id);
    }
    if (task_ptr->parent_task_id) {
        const DominoTask* parent_ptr = dominoGetTaskByID(task_ptr->parent_task_id);
        if (!parent_ptr || parent_ptr->owner_human_id != task_ptr->owner_human_id || parent_ptr->active_child_task_id != task_ptr->task_id ||
            parent_ptr->channel_index != channel || parent_ptr->status != DOMINO_TASK_STATUS_SUSPENDED ||
            (task_ptr->status != DOMINO_TASK_STATUS_RUNNING && task_ptr->status != DOMINO_TASK_STATUS_SUSPENDED)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "任务父级关系无效：父任务须存在、同属同通道并挂起等待本任务: task_id=%u, parent_task_id=%u",
                                   task_ptr->task_id, task_ptr->parent_task_id);
        }
    }
    if (task_ptr->active_child_task_id) {
        const DominoTask* child_ptr = dominoGetTaskByID(task_ptr->active_child_task_id);
        if (!child_ptr || child_ptr->parent_task_id != task_ptr->task_id) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "任务活动子级不存在或未反向引用父任务: task_id=%u, active_child=%u", task_ptr->task_id,
                                   task_ptr->active_child_task_id);
        }
    }
    if (task_ptr->pending_child_task_id) {
        const DominoTask* child_ptr = dominoGetTaskByID(task_ptr->pending_child_task_id);
        if (!child_ptr || child_ptr->owner_human_id != task_ptr->owner_human_id || child_ptr->status != DOMINO_TASK_STATUS_PLANNED ||
            child_ptr->parent_task_id || child_ptr->active_child_task_id || child_ptr->channel_index != channel) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "任务待交接子级无效：须同属同通道、未启动且无父子关系: task_id=%u, pending_child=%u",
                                   task_ptr->task_id, task_ptr->pending_child_task_id);
        }
    }
    uint8_t depth = 1U;
    const DominoTask* ancestor_ptr = task_ptr;
    bool cancel_target_is_ancestor = false;  // CHAIN 只匹配父级或更高祖先；取消自身使用 TASK。
    while (ancestor_ptr->parent_task_id) {
        if (++depth > DOMINO_HUMAN_TASK_CHAIN_DEPTH_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "任务祖先链超出深度上限或存在环: task_id=%u, limit=%u", task_ptr->task_id,
                                   DOMINO_HUMAN_TASK_CHAIN_DEPTH_MAX);
        }
        ancestor_ptr = dominoGetTaskByID(ancestor_ptr->parent_task_id);
        if (!ancestor_ptr) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "任务祖先链引用不存在的任务: task_id=%u", task_ptr->task_id);
        }
        cancel_target_is_ancestor |= ancestor_ptr->task_id == task_ptr->cancel_target_task_id;
    }
    if (task_ptr->pending_child_task_id && depth == DOMINO_HUMAN_TASK_CHAIN_DEPTH_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "任务待交接子级将超出祖先链深度上限: task_id=%u, pending_child=%u, depth=%u", task_ptr->task_id,
                               task_ptr->pending_child_task_id, depth);
    }
    if (task_ptr->cancel_target_task_id) {
        /* 本任务为 RUNNING；完整父链校验保证祖先存在、同属本人且为 SUSPENDED。 */
        if ((task_ptr->cancel_scope == DOMINO_TASK_CANCEL_TASK && task_ptr->cancel_target_task_id != task_ptr->task_id) ||
            (task_ptr->cancel_scope == DOMINO_TASK_CANCEL_CHAIN && !cancel_target_is_ancestor)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "任务取消目标与取消范围不匹配: task_id=%u, scope=%u, cancel_target=%u", task_ptr->task_id,
                                   task_ptr->cancel_scope, task_ptr->cancel_target_task_id);
        }
    }
}

void dominoHumanStoreValidate(domino_runtime_ms_t now_ms) {
    size_t listed_task_count = 0U;
    for (size_t human_index = 0U; human_index < kv_size(domino_all_human_data_list); human_index++) {
        const DominoHumanData* data_ptr = &kv_A(domino_all_human_data_list, human_index);
        validateHumanData(data_ptr);
        if (data_ptr->needs_updated_at_ms > now_ms) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "人物需求检查点晚于世界校验时刻: human_id=%u, needs_updated_at_ms=%u, now_ms=%u",
                                   data_ptr->human_id, data_ptr->needs_updated_at_ms, now_ms);
        }
        listed_task_count += data_ptr->task_count;
        for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
            const domino_task_id_t task_id = data_ptr->task_id_list[index];
            const DominoTask* task_ptr = dominoGetTaskByID(task_id);
            if (!task_ptr || task_ptr->task_id != task_id || task_ptr->owner_human_id != data_ptr->human_id) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "人物任务清单引用缺失或身份归属不一致: human_id=%u, task_id=%u", data_ptr->human_id,
                                       task_id);
            }
            const DOMINO_CODE code = dominoHumanValidateTaskRecord(task_ptr);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "人物任务记录无效: human_id=%u, task_id=%u", data_ptr->human_id, task_id);
            }
            if ((task_ptr->started_flag && task_ptr->started_at_ms > now_ms) || (isTaskTerminal(task_ptr) && task_ptr->ended_at_ms > now_ms)) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA,
                                       "任务有效开始或结束时刻晚于世界校验时刻: task_id=%u, now_ms=%u, started_at_ms=%u, ended_at_ms=%u", task_id,
                                       now_ms, task_ptr->started_at_ms, task_ptr->ended_at_ms);
            }
            for (uint8_t behavior_index = 0U; behavior_index < task_ptr->behavior_count; behavior_index++) {
                const DominoBehavior* behavior_ptr = &task_ptr->behaviors_ptr[behavior_index];
                if ((behavior_ptr->target_building_id != 0 && !dominoGetBuildingByID(behavior_ptr->target_building_id)) ||
                    (behavior_ptr->progress.started_flag && behavior_ptr->progress.accounted_at_ms > now_ms) ||
                    (isBehaviorTerminal(behavior_ptr) && behavior_ptr->progress.ended_at_ms > now_ms)) {
                    DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA,
                                           "任务行为目标建筑缺失或有效结算/结束时刻晚于世界校验时刻: task_id=%u, behavior_index=%u, building_id=%u, "
                                           "now_ms=%u, accounted_at_ms=%u, ended_at_ms=%u",
                                           task_id, behavior_index, behavior_ptr->target_building_id, now_ms, behavior_ptr->progress.accounted_at_ms,
                                           behavior_ptr->progress.ended_at_ms);
                }
            }
            validateTaskRelations(task_ptr, data_ptr);
        }
        for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
            domino_task_id_t task_id = data_ptr->active_task_id_list[channel];
            if (!task_id) {
                continue;
            }
            const DominoTask* task_ptr = dominoGetTaskByID(task_id);
            if (!task_ptr || task_ptr->owner_human_id != data_ptr->human_id || task_ptr->status != DOMINO_TASK_STATUS_RUNNING ||
                task_ptr->channel_index != channel) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "人物活动槽任务缺失、归属/通道不匹配或未运行: human_id=%u, channel=%u, task_id=%u",
                                       data_ptr->human_id, channel, task_id);
            }
        }
    }
    /* 各人的清单按 owner 隔离且各自去重，计数相等即保证全池任务恰好归属现存人物。 */
    if (listed_task_count != kv_size(domino_all_task_list)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "人物清单任务总数与任务池不一致，存在未归属任务: listed_count=%zu, pool_count=%zu",
                               listed_task_count, kv_size(domino_all_task_list));
    }
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
