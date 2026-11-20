#include "../human/state.h"
#include "../human/store.h"
#include "domino_shared_common.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/** @file serde_human_task.c
 * @brief 统一任务记录：工作身份、动机引用、回应的外部信息类别、完成条件、带通道与行为依赖的未来行为计划值、等待依据和可选执行部分。
 * @note 计划运行期版本和未回收行为记录数只属于本次运行，加载后重建；执行部分仅在 exec_status 非 NONE 时有意义。
 */

#define TASK_READ_UINT(object, record, field) ((record)->field = (typeof((record)->field))yyjson_get_uint(yyjson_obj_get(object, #field)))

static void requireField(bool ok_flag) {
    if (!ok_flag) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task fields");
    }
}

/** @brief 追加一组行为依赖；kind 为 NONE 的空项不写出。 */
static void addDependencyList(yyjson_mut_doc* doc, yyjson_mut_val* object, const DominoBehaviorDependency* dependency_list) {
    yyjson_mut_val* array = yyjson_mut_obj_add_arr(doc, object, "dependency_list");
    requireField(array != nullptr);
    for (uint8_t index = 0U; index < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; index++) {
        const DominoBehaviorDependency* dependency_ptr = &dependency_list[index];
        if (dependency_ptr->kind == DOMINO_BEHAVIOR_DEPENDENCY_NONE) {
            continue;
        }
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, array);
        requireField(item != nullptr && yyjson_mut_obj_add_uint(doc, item, "step_seq", dependency_ptr->step_seq) &&
                     yyjson_mut_obj_add_uint(doc, item, "kind", dependency_ptr->kind) &&
                     yyjson_mut_obj_add_uint(doc, item, "state", dependency_ptr->state));
    }
}

/** @brief 读取一组行为依赖；可信存档保证不超过上限。 */
static void readDependencyList(yyjson_val* object, DominoBehaviorDependency* dependency_list) {
    yyjson_val* array = yyjson_obj_get(object, "dependency_list");
    const size_t count = yyjson_arr_size(array);
    if (count > DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human behavior dependency count exceeds capacity: count=%zu", count);
    }
    for (size_t index = 0; index < count; index++) {
        yyjson_val* item = yyjson_arr_get(array, index);
        dependency_list[index] = (DominoBehaviorDependency){
            .step_seq = (uint32_t)yyjson_get_uint(yyjson_obj_get(item, "step_seq")),
            .kind = (uint8_t)yyjson_get_uint(yyjson_obj_get(item, "kind")),
            .state = (uint8_t)yyjson_get_uint(yyjson_obj_get(item, "state")),
        };
    }
}

static yyjson_mut_val* serializeTask(yyjson_mut_doc* doc, const DominoTask* task_ptr) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    requireField(object != nullptr);
    requireField(yyjson_mut_obj_add_uint(doc, object, "task_id", task_ptr->task_id) &&
                 yyjson_mut_obj_add_uint(doc, object, "owner_human_id", task_ptr->owner_human_id) &&
                 yyjson_mut_obj_add_uint(doc, object, "motivation_id", task_ptr->motivation_id) &&
                 yyjson_mut_obj_add_uint(doc, object, "type", task_ptr->type) &&
                 yyjson_mut_obj_add_uint(doc, object, "stimulus_kind", task_ptr->stimulus_kind) &&
                 yyjson_mut_obj_add_uint(doc, object, "work_source_kind", task_ptr->work_source_kind) &&
                 yyjson_mut_obj_add_uint(doc, object, "work_source_id", task_ptr->work_source_id) &&
                 yyjson_mut_obj_add_uint(doc, object, "work_round", task_ptr->work_round) &&
                 yyjson_mut_obj_add_uint(doc, object, "last_step_seq", task_ptr->last_step_seq) &&
                 yyjson_mut_obj_add_uint(doc, object, "accepted_at_ms", task_ptr->accepted_at_ms) &&
                 yyjson_mut_obj_add_uint(doc, object, "target_value", task_ptr->target_value) &&
                 yyjson_mut_obj_add_uint(doc, object, "deadline_ms", task_ptr->deadline_ms) &&
                 yyjson_mut_obj_add_uint(doc, object, "behavior_retry_at_ms", task_ptr->behavior_retry_at_ms) &&
                 yyjson_mut_obj_add_uint(doc, object, "flags", task_ptr->flags) &&
                 yyjson_mut_obj_add_uint(doc, object, "end_reason", task_ptr->end_reason) &&
                 yyjson_mut_obj_add_uint(doc, object, "exec_status", task_ptr->exec_status) &&
                 yyjson_mut_obj_add_uint(doc, object, "started_at_ms", task_ptr->started_at_ms) &&
                 yyjson_mut_obj_add_uint(doc, object, "ended_at_ms", task_ptr->ended_at_ms) &&
                 yyjson_mut_obj_add_uint(doc, object, "progress_ms", task_ptr->progress_ms) &&
                 yyjson_mut_obj_add_uint(doc, object, "last_behavior_id", task_ptr->last_behavior_id) &&
                 yyjson_mut_obj_add_uint(doc, object, "main_succeeded_count", task_ptr->main_succeeded_count) &&
                 yyjson_mut_obj_add_uint(doc, object, "main_failed_count", task_ptr->main_failed_count) &&
                 yyjson_mut_obj_add_uint(doc, object, "main_cancelled_count", task_ptr->main_cancelled_count) &&
                 yyjson_mut_obj_add_uint(doc, object, "last_behavior_end_reason", task_ptr->last_behavior_end_reason));
    yyjson_mut_val* current = yyjson_mut_obj_add_arr(doc, object, "current_behavior_id_list");
    requireField(current != nullptr);
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        requireField(yyjson_mut_arr_add_uint(doc, current, task_ptr->current_behavior_id_list[channel]));
    }
    yyjson_mut_val* plan = yyjson_mut_obj_add_arr(doc, object, "plan_item_list");
    requireField(plan != nullptr);
    for (uint8_t index = 0U; index < task_ptr->plan_count; index++) {
        const DominoBehaviorPlanItem* item_ptr = &task_ptr->plan_item_list[index];
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, plan);
        requireField(item != nullptr && yyjson_mut_obj_add_uint(doc, item, "step_seq", item_ptr->step_seq) &&
                     yyjson_mut_obj_add_uint(doc, item, "channel_index", item_ptr->channel_index) &&
                     yyjson_mut_obj_add_uint(doc, item, "flags", item_ptr->flags) && yyjson_mut_obj_add_uint(doc, item, "type", item_ptr->type) &&
                     yyjson_mut_obj_add_uint(doc, item, "target_effective_ms", item_ptr->target_effective_ms) &&
                     yyjson_mut_obj_add_uint(doc, item, "param", item_ptr->param));
        addDependencyList(doc, item, item_ptr->dependency_list);
    }
    return object;
}

/** @brief 解码可信任务及计划值；计划块由 store 导入时分配。 */
static uint8_t deserializeTask(yyjson_val* object, DominoTask* task_ptr, DominoBehaviorPlanItem* plan_list) {
    TASK_READ_UINT(object, task_ptr, task_id);
    TASK_READ_UINT(object, task_ptr, owner_human_id);
    TASK_READ_UINT(object, task_ptr, motivation_id);
    TASK_READ_UINT(object, task_ptr, type);
    TASK_READ_UINT(object, task_ptr, stimulus_kind);
    TASK_READ_UINT(object, task_ptr, work_source_kind);
    TASK_READ_UINT(object, task_ptr, work_source_id);
    TASK_READ_UINT(object, task_ptr, work_round);
    TASK_READ_UINT(object, task_ptr, last_step_seq);
    TASK_READ_UINT(object, task_ptr, accepted_at_ms);
    TASK_READ_UINT(object, task_ptr, target_value);
    TASK_READ_UINT(object, task_ptr, deadline_ms);
    TASK_READ_UINT(object, task_ptr, behavior_retry_at_ms);
    TASK_READ_UINT(object, task_ptr, flags);
    TASK_READ_UINT(object, task_ptr, end_reason);
    TASK_READ_UINT(object, task_ptr, exec_status);
    TASK_READ_UINT(object, task_ptr, started_at_ms);
    TASK_READ_UINT(object, task_ptr, ended_at_ms);
    TASK_READ_UINT(object, task_ptr, progress_ms);
    TASK_READ_UINT(object, task_ptr, last_behavior_id);
    TASK_READ_UINT(object, task_ptr, main_succeeded_count);
    TASK_READ_UINT(object, task_ptr, main_failed_count);
    TASK_READ_UINT(object, task_ptr, main_cancelled_count);
    TASK_READ_UINT(object, task_ptr, last_behavior_end_reason);
    yyjson_val* current = yyjson_obj_get(object, "current_behavior_id_list");
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        task_ptr->current_behavior_id_list[channel] = (domino_behavior_id_t)yyjson_get_uint(yyjson_arr_get(current, channel));
    }
    yyjson_val* plan = yyjson_obj_get(object, "plan_item_list");
    const size_t plan_count = yyjson_arr_size(plan);
    if (plan_count > DOMINO_TASK_STEP_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human task plan exceeds capacity: task_id=%u", task_ptr->task_id);
    }
    for (size_t index = 0; index < plan_count; index++) {
        yyjson_val* item = yyjson_arr_get(plan, index);
        plan_list[index] = (DominoBehaviorPlanItem){
            .target_effective_ms = (domino_runtime_ms_t)yyjson_get_uint(yyjson_obj_get(item, "target_effective_ms")),
            .param = (uint32_t)yyjson_get_uint(yyjson_obj_get(item, "param")),
            .step_seq = (uint32_t)yyjson_get_uint(yyjson_obj_get(item, "step_seq")),
            .type = (uint8_t)yyjson_get_uint(yyjson_obj_get(item, "type")),
            .channel_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(item, "channel_index")),
            .flags = (uint8_t)yyjson_get_uint(yyjson_obj_get(item, "flags")),
        };
        readDependencyList(item, plan_list[index].dependency_list);
    }
    return (uint8_t)plan_count;
}

#undef TASK_READ_UINT

void serdeSaveHumanTask(const char* save_path) {
    const size_t total_count = kv_size(domino_all_task_list);
    if (total_count > UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human task count exceeds storage limit: count=%zu", total_count);
    }
    const uint32_t shard_count = storageEntityShardCount((uint32_t)total_count, DOMINO_STORAGE_SHARD_HUMAN_TASK);
    for (uint32_t shard = 0U; shard < shard_count; shard++) {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task JSON document: shard=%u", shard);
        }
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        if (!root) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task JSON root: shard=%u", shard);
        }
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_val* entities = yyjson_mut_obj_add_arr(doc, root, "entities");
        if (!entities || !yyjson_mut_obj_add_str(doc, root, "type", "human_task") || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task shard fields: shard=%u", shard);
        }
        const size_t start = (size_t)shard * DOMINO_STORAGE_SHARD_HUMAN_TASK;
        const size_t remaining_count = total_count - start;
        const size_t count = remaining_count < DOMINO_STORAGE_SHARD_HUMAN_TASK ? remaining_count : DOMINO_STORAGE_SHARD_HUMAN_TASK;
        for (size_t index = start; index < start + count; index++) {
            if (!yyjson_mut_arr_append(entities, serializeTask(doc, &kv_A(domino_all_task_list, index)))) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot serialize human task record: task_id=%u", kv_A(domino_all_task_list, index).task_id);
            }
        }
        storageWriteShardFile("human_task", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

/** @brief 按全局顺序直接调用 human store 导入可信任务及计划值并建立 ID 索引；人物清单、运行任务与待执行队列由 human.data 恢复。 */
void serdeLoadHumanTask(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    storageValidateLoadCount(total_count, sizeof(DominoTask));
    for (uint32_t shard = 0U; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human_task", shard, save_path);
        yyjson_val* entities = yyjson_obj_get(yyjson_doc_get_root(doc), "entities");
        size_t index;
        size_t count;
        yyjson_val* object;
        yyjson_arr_foreach(entities, index, count, object) {
            DominoTask task = {0};
            DominoBehaviorPlanItem plan_list[DOMINO_TASK_STEP_COUNT_MAX];
            const uint8_t plan_count = deserializeTask(object, &task, plan_list);
            const DOMINO_CODE code = dominoHumanStoreImportTask(&task, plan_list, plan_count);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "Cannot import human task: task_id=%u", task.task_id);
            }
        }
        yyjson_doc_free(doc);
    }
}
