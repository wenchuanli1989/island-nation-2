#include <stdlib.h>

#include "../human/state.h"
#include "../human/store.h"
#include "domino_shared_common.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

#define READ(obj, ptr, field) ((ptr)->field = (typeof((ptr)->field))yyjson_get_uint(yyjson_obj_get(obj, #field)))
#define WRITE(obj, ptr, field)                                                     \
    do {                                                                           \
        if (!yyjson_mut_obj_add_uint(doc, obj, #field, (ptr)->field))              \
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot serialize human AI"); \
    } while (0)
static void readCheck(yyjson_val* obj, DominoHumanCheckTime* value) {
    READ(obj, value, at_ms);
    value->valid = yyjson_get_bool(yyjson_obj_get(obj, "valid"));
}
static yyjson_mut_val* writeCheck(yyjson_mut_doc* doc, DominoHumanCheckTime value) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    if (!obj || !yyjson_mut_obj_add_uint(doc, obj, "at_ms", value.at_ms) || !yyjson_mut_obj_add_bool(doc, obj, "valid", value.valid))
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot serialize check time");
    return obj;
}
static void deserializeTask(yyjson_val* object, DominoTask* value) {
    READ(object, value, task_id);
    READ(object, value, owner_human_id);
    READ(object, value, motivation_id);
    READ(object, value, work_round);
    READ(object, value, first_record_id);
    READ(object, value, record_count);
    READ(object, value, type);
    READ(object, value, channel_index);
    READ(object, value, target_energy);
    READ(object, value, waiting_condition);
    READ(object, value, finish_inclusive);
    readCheck(yyjson_obj_get(object, "retry"), &value->retry);
    readCheck(yyjson_obj_get(object, "not_before"), &value->not_before);
    readCheck(yyjson_obj_get(object, "start_deadline"), &value->start_deadline);
    readCheck(yyjson_obj_get(object, "finish_deadline"), &value->finish_deadline);
    yyjson_val* execution = yyjson_obj_get(object, "execution");
    value->has_execution = yyjson_is_obj(execution);
    if (value->has_execution) {
        READ(execution, &value->execution, successful_behavior_count);
        READ(execution, &value->execution, completed_active_ms);
        READ(execution, &value->execution, current_behavior_id);
        READ(execution, &value->execution, started_at_ms);
        READ(execution, &value->execution, ended_at_ms);
        READ(execution, &value->execution, status);
        READ(execution, &value->execution, last_reason);
    }
    yyjson_val* plans = yyjson_obj_get(object, "plans");
    size_t plan_count = yyjson_arr_size(plans);
    if (plan_count + value->record_count > DOMINO_TASK_BEHAVIOR_COUNT_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "task step capacity");
    value->plan_count = (uint16_t)plan_count;
    if (plan_count) {
        value->plans = malloc(plan_count * sizeof(*value->plans));
        if (!value->plans)
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot load plans");
        for (size_t i = 0; i < plan_count; ++i) {
            yyjson_val* row = yyjson_arr_get(plans, i);
            READ(row, &value->plans[i], type);
            READ(row, &value->plans[i], duration_ms);
        }
    }
    yyjson_val* deps = yyjson_obj_get(object, "dependencies");
    size_t dep_count = yyjson_arr_size(deps);
    if (dep_count > DOMINO_TASK_DEPENDENCY_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "dependency capacity");
    value->dependency_count = (uint8_t)dep_count;
    for (size_t i = 0; i < dep_count; ++i) {
        yyjson_val* row = yyjson_arr_get(deps, i);
        READ(row, &value->dependencies[i], task_id);
        READ(row, &value->dependencies[i], condition);
        READ(row, &value->dependencies[i], stages);
        READ(row, &value->dependencies[i], resolved_result);
    }
}
static yyjson_mut_val* serializeTask(yyjson_mut_doc* doc, const DominoTask* value) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot serialize task");
    WRITE(object, value, task_id);
    WRITE(object, value, owner_human_id);
    WRITE(object, value, motivation_id);
    WRITE(object, value, work_round);
    WRITE(object, value, first_record_id);
    WRITE(object, value, record_count);
    WRITE(object, value, type);
    WRITE(object, value, channel_index);
    WRITE(object, value, target_energy);
    WRITE(object, value, waiting_condition);
    WRITE(object, value, finish_inclusive);
    if (!yyjson_mut_obj_add_val(doc, object, "retry", writeCheck(doc, value->retry)))
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "check time field");
    if (!yyjson_mut_obj_add_val(doc, object, "not_before", writeCheck(doc, value->not_before)))
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "check time field");
    if (!yyjson_mut_obj_add_val(doc, object, "start_deadline", writeCheck(doc, value->start_deadline)))
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "check time field");
    if (!yyjson_mut_obj_add_val(doc, object, "finish_deadline", writeCheck(doc, value->finish_deadline)))
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "check time field");
    if (value->has_execution) {
        yyjson_mut_val* execution = yyjson_mut_obj_add_obj(doc, object, "execution");
        if (!execution)
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "execution field");
        WRITE(execution, &value->execution, successful_behavior_count);
        WRITE(execution, &value->execution, completed_active_ms);
        WRITE(execution, &value->execution, current_behavior_id);
        WRITE(execution, &value->execution, started_at_ms);
        WRITE(execution, &value->execution, ended_at_ms);
        WRITE(execution, &value->execution, status);
        WRITE(execution, &value->execution, last_reason);
    }
    yyjson_mut_val* plans = yyjson_mut_obj_add_arr(doc, object, "plans");
    yyjson_mut_val* deps = yyjson_mut_obj_add_arr(doc, object, "dependencies");
    if (!plans || !deps)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "task arrays");
    for (uint16_t i = 0; i < value->plan_count; ++i) {
        yyjson_mut_val* row = yyjson_mut_obj(doc);
        if (!row || !yyjson_mut_arr_append(plans, row))
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "plan row");
        WRITE(row, &value->plans[i], type);
        WRITE(row, &value->plans[i], duration_ms);
    }
    for (uint8_t i = 0; i < value->dependency_count; ++i) {
        yyjson_mut_val* row = yyjson_mut_obj(doc);
        if (!row || !yyjson_mut_arr_append(deps, row))
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "dependency row");
        WRITE(row, &value->dependencies[i], task_id);
        WRITE(row, &value->dependencies[i], condition);
        WRITE(row, &value->dependencies[i], stages);
        WRITE(row, &value->dependencies[i], resolved_result);
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

/** @brief 按全局顺序直接调用 human store 导入可信任务并建立 ID 索引；人物任务清单由 human.data 恢复，不在此重建。 */
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
