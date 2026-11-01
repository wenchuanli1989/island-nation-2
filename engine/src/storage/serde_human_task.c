#include "../human/state.h"
#include "../human/store.h"
#include "domino_shared_common.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/* 仅从当前格式的可信字段构造记录；容量与资源检查由所属加载步骤承担。 */
#define TASK_READ_UINT(object, record, field) ((record)->field = (typeof((record)->field))yyjson_get_uint(yyjson_obj_get(object, #field)))
#define TASK_READ_BOOL(object, record, field) ((record)->field = yyjson_get_bool(yyjson_obj_get(object, #field)))

/** @brief 按可信行为 ID 数组恢复任务序列；行为实体由独立分片导入。 */
static void deserializeTask(yyjson_val* object, DominoTask* task_ptr) {
    TASK_READ_UINT(object, task_ptr, task_id);
    TASK_READ_UINT(object, task_ptr, owner_human_id);
    TASK_READ_UINT(object, task_ptr, planned_begin_ms);
    TASK_READ_UINT(object, task_ptr, planned_end_ms);
    TASK_READ_UINT(object, task_ptr, started_at_ms);
    TASK_READ_UINT(object, task_ptr, ended_at_ms);
    TASK_READ_UINT(object, task_ptr, current_behavior_id);
    TASK_READ_UINT(object, task_ptr, type);
    TASK_READ_UINT(object, task_ptr, channel_index);
    TASK_READ_UINT(object, task_ptr, status);
    TASK_READ_UINT(object, task_ptr, priority_class);
    TASK_READ_BOOL(object, task_ptr, started_flag);
    yyjson_val* behaviors = yyjson_obj_get(object, "behavior_id_list");
    size_t count = yyjson_arr_size(behaviors);
    if (count > DOMINO_TASK_BEHAVIOR_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human task behavior count exceeds task capacity: task_id=%u count=%zu", task_ptr->task_id,
                               count);
    }
    task_ptr->behavior_count = (uint8_t)count;
    serdeReadU32Array(behaviors, task_ptr->behavior_id_list, DOMINO_TASK_BEHAVIOR_COUNT_MAX);
}

#undef TASK_READ_UINT
#undef TASK_READ_BOOL

/** @brief 显式写入任务事实及行为 ID。 */
static yyjson_mut_val* serializeTask(yyjson_mut_doc* doc, const DominoTask* task) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "task_id", task->task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "owner_human_id", task->owner_human_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_begin_ms", task->planned_begin_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_end_ms", task->planned_end_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "started_at_ms", task->started_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "ended_at_ms", task->ended_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "current_behavior_id", task->current_behavior_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "channel_index", task->channel_index) || !yyjson_mut_obj_add_uint(doc, object, "type", task->type) ||
        !yyjson_mut_obj_add_uint(doc, object, "status", task->status) ||
        !yyjson_mut_obj_add_uint(doc, object, "priority_class", task->priority_class) ||
        !yyjson_mut_obj_add_bool(doc, object, "started_flag", task->started_flag)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task fields");
    }
    yyjson_mut_val* behaviors = yyjson_mut_obj_add_arr(doc, object, "behavior_id_list");
    if (!behaviors) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task fields");
    }
    for (uint32_t index = 0; index < task->behavior_count; index++) {
        if (!yyjson_mut_arr_add_uint(doc, behaviors, task->behavior_id_list[index])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task fields");
        }
    }
    return object;
}

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
            yyjson_mut_val* object = serializeTask(doc, &kv_A(domino_all_task_list, index));
            if (!yyjson_mut_arr_append(entities, object)) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot serialize human task record: task_id=%u", kv_A(domino_all_task_list, index).task_id);
            }
        }
        storageWriteShardFile("human_task", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

/** @brief 按全局顺序导入可信任务并建立 ID 索引；人物任务清单由 human.data 恢复，不在此重建。 */
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
            deserializeTask(object, &task);
            const DOMINO_CODE code = dominoHumanStoreImportTask(&task);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "Cannot import human task: task_id=%u", task.task_id);
            }
        }
        yyjson_doc_free(doc);
    }
}
