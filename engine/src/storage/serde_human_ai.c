#include <stdlib.h>

#include "../human/entry.h"
#include "../human/store.h"
#include "../logger/entry.h"
#include "../time/entry.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/** @brief AI 字段必须存在且在目标类型范围内；不使用 yyjson 的缺失字段默认值。 */
static DOMINO_CODE readUnsigned(yyjson_val* object, const char* key, uint64_t maximum, uint64_t* value_ptr) {
    yyjson_val* value = yyjson_obj_get(object, key);
    if (!yyjson_is_uint(value)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI field must be an unsigned integer: field=%s", key);
        return ERR_INVALID_DATA;
    }
    uint64_t parsed = yyjson_get_uint(value);
    if (parsed > maximum) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                          "Human AI field exceeds its allowed range: field=%s value=%llu maximum=%llu", key, (unsigned long long)parsed,
                          (unsigned long long)maximum);
        return ERR_OUT_OF_RANGE;
    }
    *value_ptr = parsed;
    return CODE_OK;
}

/* 各字段函数只解析字段，不分配堆内存；字段错误在源头记录，由外层加载循环直接传播。 */
#define AI_READ_UINT(object, record, field, maximum)                                    \
    do {                                                                                \
        uint64_t parsed_value;                                                          \
        DOMINO_CODE read_result = readUnsigned(object, #field, maximum, &parsed_value); \
        if (read_result != CODE_OK) {                                                   \
            return read_result;                                                         \
        }                                                                               \
        (record)->field = (typeof((record)->field))parsed_value;                        \
    } while (0)

#define AI_READ_BOOL(object, record, field)                                                                                                    \
    do {                                                                                                                                       \
        yyjson_val* parsed_value = yyjson_obj_get(object, #field);                                                                             \
        if (!yyjson_is_bool(parsed_value)) {                                                                                                   \
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI field must be a boolean: field=%s", #field); \
            return ERR_INVALID_DATA;                                                                                                           \
        }                                                                                                                                      \
        (record)->field = yyjson_get_bool(parsed_value);                                                                                       \
    } while (0)

static DOMINO_CODE deserializeProgress(yyjson_val* object, DominoBehaviorProgress* progress_ptr) {
    if (!yyjson_is_obj(object)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI behavior progress must be an object");
        return ERR_INVALID_DATA;
    }
    AI_READ_UINT(object, progress_ptr, started_at_ms, UINT32_MAX);
    AI_READ_UINT(object, progress_ptr, ended_at_ms, UINT32_MAX);
    AI_READ_UINT(object, progress_ptr, accounted_at_ms, UINT32_MAX);
    AI_READ_UINT(object, progress_ptr, active_elapsed_ms, UINT32_MAX);
    AI_READ_UINT(object, progress_ptr, wait_until_ms, UINT32_MAX);
    AI_READ_UINT(object, progress_ptr, status, UINT8_MAX);
    AI_READ_UINT(object, progress_ptr, wait_reason, UINT8_MAX);
    AI_READ_BOOL(object, progress_ptr, started_flag);
    AI_READ_BOOL(object, progress_ptr, effect_committed_flag);
    return CODE_OK;
}

static DOMINO_CODE deserializeBehavior(yyjson_val* object, DominoBehavior* behavior_ptr) {
    if (!yyjson_is_obj(object)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI behavior must be an object");
        return ERR_INVALID_DATA;
    }
    AI_READ_UINT(object, behavior_ptr, type, DOMINO_BEHAVIOR_TYPE_COUNT - 1U);
    AI_READ_UINT(object, behavior_ptr, target_building_id, UINT32_MAX);
    AI_READ_UINT(object, behavior_ptr, planned_begin_ms, UINT32_MAX);
    AI_READ_UINT(object, behavior_ptr, planned_end_ms, UINT32_MAX);
    yyjson_val* parameters = yyjson_obj_get(object, "parameters");
    if (!yyjson_is_obj(parameters)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI behavior parameters must be an object");
        return ERR_INVALID_DATA;
    }
    if (behavior_ptr->type == DOMINO_BEHAVIOR_TYPE_SLEEP) {
        AI_READ_UINT(parameters, &behavior_ptr->parameters.sleep, target_active_ms, UINT32_MAX);
    }
    return deserializeProgress(yyjson_obj_get(object, "progress"), &behavior_ptr->progress);
}

/** @brief 行为数量以嵌套数组为唯一来源，完成字段解析后由调用方分配连续块。 */
static DOMINO_CODE deserializeTask(yyjson_val* object, DominoTask* task_ptr) {
    if (!yyjson_is_obj(object)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI task must be an object");
        return ERR_INVALID_DATA;
    }
    AI_READ_UINT(object, task_ptr, task_id, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, owner_human_id, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, parent_task_id, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, active_child_task_id, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, pending_child_task_id, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, cancel_target_task_id, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, planned_begin_ms, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, planned_end_ms, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, started_at_ms, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, ended_at_ms, UINT32_MAX);
    AI_READ_UINT(object, task_ptr, current_behavior_index, UINT8_MAX);
    AI_READ_UINT(object, task_ptr, type, DOMINO_TASK_TYPE_COUNT - 1U);
    AI_READ_UINT(object, task_ptr, channel_index, DOMINO_HUMAN_TASK_CHANNEL_COUNT - 1U);
    AI_READ_UINT(object, task_ptr, status, UINT8_MAX);
    AI_READ_UINT(object, task_ptr, result, UINT8_MAX);
    AI_READ_UINT(object, task_ptr, priority_class, UINT8_MAX);
    AI_READ_UINT(object, task_ptr, pending_stop_reason, UINT8_MAX);
    AI_READ_UINT(object, task_ptr, cancel_scope, UINT8_MAX);
    AI_READ_BOOL(object, task_ptr, started_flag);
    yyjson_val* behaviors = yyjson_obj_get(object, "behaviors");
    if (!yyjson_is_arr(behaviors)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI behaviors must be an array: task_id=%u",
                          task_ptr->task_id);
        return ERR_INVALID_DATA;
    }
    size_t count = yyjson_arr_size(behaviors);
    if (count == 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI task has no behaviors: task_id=%u", task_ptr->task_id);
        return ERR_INVALID_DATA;
    }
    if (count > DOMINO_TASK_BEHAVIOR_COUNT_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                          "Human AI behavior count exceeds task capacity: task_id=%u count=%zu", task_ptr->task_id, count);
        return ERR_CAPACITY_EXCEEDED;
    }
    task_ptr->behavior_count = (uint8_t)count;
    return CODE_OK;
}

#undef AI_READ_UINT
#undef AI_READ_BOOL

static yyjson_mut_val* serializeProgress(yyjson_mut_doc* doc, const DominoBehaviorProgress* progress) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "started_at_ms", progress->started_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "ended_at_ms", progress->ended_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "accounted_at_ms", progress->accounted_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "active_elapsed_ms", progress->active_elapsed_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "wait_until_ms", progress->wait_until_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "status", progress->status) ||
        !yyjson_mut_obj_add_uint(doc, object, "wait_reason", progress->wait_reason) ||
        !yyjson_mut_obj_add_bool(doc, object, "started_flag", progress->started_flag) ||
        !yyjson_mut_obj_add_bool(doc, object, "effect_committed_flag", progress->effect_committed_flag)) {
        return nullptr;
    }
    return object;
}

/** @brief 写入行为公共字段，按类型写入已定义的专属参数。 */
static yyjson_mut_val* serializeBehavior(yyjson_mut_doc* doc, const DominoBehavior* behavior) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object) {
        return nullptr;
    }
    yyjson_mut_val* parameters = yyjson_mut_obj(doc);
    if (!parameters) {
        return nullptr;
    }
    yyjson_mut_val* progress = serializeProgress(doc, &behavior->progress);
    if (!progress || !yyjson_mut_obj_add_uint(doc, object, "type", behavior->type) ||
        !yyjson_mut_obj_add_uint(doc, object, "target_building_id", behavior->target_building_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_begin_ms", behavior->planned_begin_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_end_ms", behavior->planned_end_ms) ||
        !yyjson_mut_obj_add_val(doc, object, "parameters", parameters) || !yyjson_mut_obj_add_val(doc, object, "progress", progress)) {
        return nullptr;
    }
    if (behavior->type == DOMINO_BEHAVIOR_TYPE_SLEEP &&
        !yyjson_mut_obj_add_uint(doc, parameters, "target_active_ms", behavior->parameters.sleep.target_active_ms)) {
        return nullptr;
    }
    return object;
}

/** @brief 显式写入任务持久化字段和行为内容，不写 behaviors_ptr 地址与 execution_version。 */
static yyjson_mut_val* serializeTask(yyjson_mut_doc* doc, const DominoTask* task) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "task_id", task->task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "owner_human_id", task->owner_human_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "parent_task_id", task->parent_task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "active_child_task_id", task->active_child_task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "pending_child_task_id", task->pending_child_task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "cancel_target_task_id", task->cancel_target_task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_begin_ms", task->planned_begin_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_end_ms", task->planned_end_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "started_at_ms", task->started_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "ended_at_ms", task->ended_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "current_behavior_index", task->current_behavior_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "channel_index", task->channel_index) || !yyjson_mut_obj_add_uint(doc, object, "type", task->type) ||
        !yyjson_mut_obj_add_uint(doc, object, "status", task->status) || !yyjson_mut_obj_add_uint(doc, object, "result", task->result) ||
        !yyjson_mut_obj_add_uint(doc, object, "priority_class", task->priority_class) ||
        !yyjson_mut_obj_add_uint(doc, object, "pending_stop_reason", task->pending_stop_reason) ||
        !yyjson_mut_obj_add_uint(doc, object, "cancel_scope", task->cancel_scope) ||
        !yyjson_mut_obj_add_bool(doc, object, "started_flag", task->started_flag)) {
        return nullptr;
    }
    yyjson_mut_val* behaviors = yyjson_mut_obj_add_arr(doc, object, "behaviors");
    if (!behaviors) {
        return nullptr;
    }
    for (uint32_t index = 0; index < task->behavior_count; index++) {
        yyjson_mut_val* behavior = serializeBehavior(doc, &task->behaviors_ptr[index]);
        if (!behavior || !yyjson_mut_arr_append(behaviors, behavior)) {
            return nullptr;
        }
    }
    return object;
}

/** @brief 人物 AI 事实归属 human.data；此分片仅保存人物 ID 及其任务和行为。 */
static yyjson_mut_val* serializeAI(yyjson_mut_doc* doc, const DominoHumanData* human_data) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "human_id", human_data->human_id)) {
        return nullptr;
    }
    yyjson_mut_val* tasks = yyjson_mut_obj_add_arr(doc, object, "tasks");
    if (!tasks) {
        return nullptr;
    }
    for (uint32_t index = 0; index < human_data->task_count; index++) {
        yyjson_mut_val* task = serializeTask(doc, dominoGetTaskByID(human_data->task_id_list[index]));
        if (!task || !yyjson_mut_arr_append(tasks, task)) {
            return nullptr;
        }
    }
    return object;
}

/**
 * @brief 任务清单已由 human.data 恢复，此处只导入任务实体；已导入的行为块归 store。
 * 失败直接上报，已导入内容不回滚。
 */
static DOMINO_CODE loadOneAI(yyjson_val* object, bool* seen_list) {
    if (!yyjson_is_obj(object)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI record must be an object");
        return ERR_INVALID_DATA;
    }
    uint64_t parsed_human_id;
    DOMINO_CODE result = readUnsigned(object, "human_id", UINT32_MAX, &parsed_human_id);
    if (result != CODE_OK) {
        return result;
    }
    domino_human_id_t human_id = (domino_human_id_t)parsed_human_id;
    khint_t key = dominoHumanIdMap_get(dominoHumanIdMap, human_id);
    if (!kh_exist(dominoHumanIdMap, key)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI record references a missing human: human_id=%u",
                          human_id);
        return ERR_INVALID_DATA;
    }
    uint32_t human_index = kh_val(dominoHumanIdMap, key);
    if (seen_list[human_index]) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Duplicate human AI record: human_id=%u", human_id);
        return ERR_ALREADY_EXISTS;
    }
    yyjson_val* tasks = yyjson_obj_get(object, "tasks");
    if (!yyjson_is_arr(tasks)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI tasks must be an array: human_id=%u", human_id);
        return ERR_INVALID_DATA;
    }
    if (yyjson_arr_size(tasks) > DOMINO_HUMAN_TASK_COUNT_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI task count exceeds capacity: human_id=%u count=%zu",
                          human_id, yyjson_arr_size(tasks));
        return ERR_CAPACITY_EXCEEDED;
    }
    size_t current_task_count = kv_size(domino_all_task_list);
    size_t incoming_task_count = yyjson_arr_size(tasks);
    if (incoming_task_count > SIZE_MAX - current_task_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI task count overflows: current=%zu incoming=%zu",
                          current_task_count, incoming_task_count);
        return ERR_MEMORY_OVERFLOW;
    }
    size_t required_task_count = current_task_count + incoming_task_count;
    const size_t maximum_task_capacity = SIZE_MAX / sizeof(DominoTask);
    if (required_task_count > maximum_task_capacity) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Human AI task storage size overflows: count=%zu",
                          required_task_count);
        return ERR_MEMORY_OVERFLOW;
    }
    size_t task_index;
    size_t task_count;
    yyjson_val* task_object;
    yyjson_arr_foreach(tasks, task_index, task_count, task_object) {
        DominoTask task = {0};
        result = deserializeTask(task_object, &task);
        if (result != CODE_OK) {
            return result;
        }
        if (task.owner_human_id != human_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                              "Human AI task owner disagrees with record: task_id=%u owner=%u human_id=%u", task.task_id, task.owner_human_id,
                              human_id);
            return ERR_INVALID_DATA;
        }
        task.behaviors_ptr = calloc(task.behavior_count, sizeof(*task.behaviors_ptr));
        if (!task.behaviors_ptr) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Cannot allocate human AI behaviors: task_id=%u count=%u",
                              task.task_id, task.behavior_count);
            return ERR_MEMORY_ALLOC;
        }
        yyjson_val* behaviors = yyjson_obj_get(task_object, "behaviors");
        for (uint32_t index = 0; index < task.behavior_count; index++) {
            result = deserializeBehavior(yyjson_arr_get(behaviors, index), &task.behaviors_ptr[index]);
            if (result != CODE_OK) {
                return result;
            }
        }
        result = dominoHumanStoreImportTask(&task);
        if (result != CODE_OK) {
            return result;
        }
    }
    seen_list[human_index] = true;
    return CODE_OK;
}

DOMINO_CODE serdeValidateHumanAIReferences(void) {
    DOMINO_CODE result = serdeValidateHumanReferences();
    if (result != CODE_OK) {
        return result;
    }
    domino_runtime_ms_t now_ms;
    result = dominoTimeGetRuntimeMs(&now_ms);
    return result == CODE_OK ? dominoHumanStoreValidate(now_ms) : result;
}

void serdeBindHumanAIReferences(void) {
    dominoHumanStoreResetRuntime();
}

DOMINO_CODE serdeSaveHumanAI(const char* save_path) {
    DOMINO_CODE result = serdeValidateHumanAIReferences();
    if (result != CODE_OK) {
        return result;
    }
    /* 引用预检已确认人物数量等于使用 32 位计数的 ID map。 */
    const size_t total_count = kv_size(domino_all_human_list);
    uint32_t shard_count = storageEntityShardCount((uint32_t)total_count, DOMINO_STORAGE_SHARD_HUMAN_AI);
    for (uint32_t shard = 0; shard < shard_count; shard++) {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Cannot allocate human AI JSON document: shard=%u", shard);
            return ERR_MEMORY_ALLOC;
        }
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        if (!root) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Cannot allocate human AI JSON root: shard=%u", shard);
            return ERR_MEMORY_ALLOC;
        }
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_val* entities = yyjson_mut_obj_add_arr(doc, root, "entities");
        if (!entities || !yyjson_mut_obj_add_str(doc, root, "type", "human_ai") || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Cannot allocate human AI shard fields: shard=%u", shard);
            return ERR_MEMORY_ALLOC;
        }
        size_t start = (size_t)shard * DOMINO_STORAGE_SHARD_HUMAN_AI;
        size_t end = start + DOMINO_STORAGE_SHARD_HUMAN_AI;
        if (end > total_count) {
            end = total_count;
        }
        for (size_t index = start; index < end; index++) {
            yyjson_mut_val* object = serializeAI(doc, &kv_A(domino_all_human_data_list, index));
            if (!object || !yyjson_mut_arr_append(entities, object)) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Cannot serialize human AI record: human_id=%u",
                                  kv_A(domino_all_human_data_list, index).human_id);
                return ERR_MEMORY_ALLOC;
            }
        }
        result = storageWriteShardFile("human_ai", shard, save_path, doc);
        if (result != CODE_OK) {
            return result;
        }
        yyjson_mut_doc_free(doc);
    }
    return CODE_OK;
}

DOMINO_CODE serdeLoadHumanAI(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    if (kv_size(domino_all_task_list) != 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Cannot load human AI into a nonempty task store");
        return ERR_GAME_STATE_INVALID;
    }
    if (total_count != kv_size(domino_all_human_list) || shard_count != storageEntityShardCount(total_count, DOMINO_STORAGE_SHARD_HUMAN_AI)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                          "Human AI metadata disagrees with human records or shard count: total=%u humans=%zu shards=%u", total_count,
                          kv_size(domino_all_human_list), shard_count);
        return ERR_INVALID_DATA;
    }
    if (total_count == 0U) {
        return CODE_OK;
    }
    /* 仅加载期记录人物是否已有任务集合；通过人物索引允许分片中的记录任意排序。 */
    bool* seen_list = calloc(total_count, sizeof(*seen_list));
    if (!seen_list) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "Cannot allocate human AI duplicate tracking: count=%u",
                          total_count);
        return ERR_MEMORY_ALLOC;
    }
    DOMINO_CODE result = CODE_OK;
    uint32_t loaded_count = 0;
    for (uint32_t shard = 0; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human_ai", shard, save_path, &result);
        if (result != CODE_OK) {
            return result;
        }
        yyjson_val* entities = serdeValidateShardRoot(yyjson_doc_get_root(doc), "human_ai", shard);
        uint32_t remaining = total_count - loaded_count;
        uint32_t expected = remaining < DOMINO_STORAGE_SHARD_HUMAN_AI ? remaining : DOMINO_STORAGE_SHARD_HUMAN_AI;
        if (!entities) {
            return ERR_INVALID_DATA;
        }
        if (yyjson_arr_size(entities) != expected) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                              "Human AI shard record count disagrees with metadata: shard=%u expected=%u actual=%zu", shard, expected,
                              yyjson_arr_size(entities));
            return ERR_INVALID_DATA;
        }
        size_t index;
        size_t count;
        yyjson_val* object;
        yyjson_arr_foreach(entities, index, count, object) {
            result = loadOneAI(object, seen_list);
            if (result != CODE_OK) {
                return result;
            }
            loaded_count++;
        }
        yyjson_doc_free(doc);
    }
    /* 每片已精确校验记录数；人物 ID 存在且去重，完成全部分片即覆盖全部人物。 */
    free(seen_list);
    return CODE_OK;
}
