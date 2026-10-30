#include <stdlib.h>

#include "../human/entry.h"
#include "../human/store.h"
#include "domino_shared_common.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/* 仅从当前格式的可信字段构造记录；容量与资源检查由所属加载步骤承担。 */
#define AI_READ_UINT(object, record, field) ((record)->field = (typeof((record)->field))yyjson_get_uint(yyjson_obj_get(object, #field)))
#define AI_READ_BOOL(object, record, field) ((record)->field = yyjson_get_bool(yyjson_obj_get(object, #field)))

static void deserializeBehavior(yyjson_val* object, DominoBehavior* behavior_ptr) {
    AI_READ_UINT(object, behavior_ptr, type);
    AI_READ_UINT(object, behavior_ptr, planned_begin_ms);
    AI_READ_UINT(object, behavior_ptr, planned_end_ms);
    AI_READ_UINT(object, behavior_ptr, started_at_ms);
    AI_READ_UINT(object, behavior_ptr, ended_at_ms);
    AI_READ_UINT(object, behavior_ptr, accounted_at_ms);
    AI_READ_UINT(object, behavior_ptr, active_elapsed_ms);
    AI_READ_UINT(object, behavior_ptr, status);
    AI_READ_BOOL(object, behavior_ptr, started_flag);
}

/** @brief 行为数量以嵌套数组为唯一来源，完成字段解析后由调用方分配连续块。 */
static void deserializeTask(yyjson_val* object, DominoTask* task_ptr) {
    AI_READ_UINT(object, task_ptr, task_id);
    AI_READ_UINT(object, task_ptr, owner_human_id);
    AI_READ_UINT(object, task_ptr, planned_begin_ms);
    AI_READ_UINT(object, task_ptr, planned_end_ms);
    AI_READ_UINT(object, task_ptr, started_at_ms);
    AI_READ_UINT(object, task_ptr, ended_at_ms);
    AI_READ_UINT(object, task_ptr, current_behavior_index);
    AI_READ_UINT(object, task_ptr, type);
    AI_READ_UINT(object, task_ptr, channel_index);
    AI_READ_UINT(object, task_ptr, status);
    AI_READ_UINT(object, task_ptr, priority_class);
    AI_READ_BOOL(object, task_ptr, started_flag);
    yyjson_val* behaviors = yyjson_obj_get(object, "behaviors");
    size_t count = yyjson_arr_size(behaviors);
    if (count > DOMINO_TASK_BEHAVIOR_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human AI behavior count exceeds task capacity: task_id=%u count=%zu", task_ptr->task_id,
                               count);
    }
    task_ptr->behavior_count = (uint8_t)count;
}

#undef AI_READ_UINT
#undef AI_READ_BOOL

/** @brief 写入行为事实；睡眠目标时长由计划区间推导，不单独保存。 */
static yyjson_mut_val* serializeBehavior(yyjson_mut_doc* doc, const DominoBehavior* behavior) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI behavior fields");
    }
    if (!yyjson_mut_obj_add_uint(doc, object, "type", behavior->type) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_begin_ms", behavior->planned_begin_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_end_ms", behavior->planned_end_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "started_at_ms", behavior->started_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "ended_at_ms", behavior->ended_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "accounted_at_ms", behavior->accounted_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "active_elapsed_ms", behavior->active_elapsed_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "status", behavior->status) ||
        !yyjson_mut_obj_add_bool(doc, object, "started_flag", behavior->started_flag)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI behavior fields");
    }
    return object;
}

/** @brief 显式写入任务持久化字段和行为内容，不写 behaviors_ptr 地址与 execution_version。 */
static yyjson_mut_val* serializeTask(yyjson_mut_doc* doc, const DominoTask* task) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "task_id", task->task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "owner_human_id", task->owner_human_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_begin_ms", task->planned_begin_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_end_ms", task->planned_end_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "started_at_ms", task->started_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "ended_at_ms", task->ended_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "current_behavior_index", task->current_behavior_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "channel_index", task->channel_index) || !yyjson_mut_obj_add_uint(doc, object, "type", task->type) ||
        !yyjson_mut_obj_add_uint(doc, object, "status", task->status) ||
        !yyjson_mut_obj_add_uint(doc, object, "priority_class", task->priority_class) ||
        !yyjson_mut_obj_add_bool(doc, object, "started_flag", task->started_flag)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI task fields");
    }
    yyjson_mut_val* behaviors = yyjson_mut_obj_add_arr(doc, object, "behaviors");
    if (!behaviors) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI task fields");
    }
    for (uint32_t index = 0; index < task->behavior_count; index++) {
        yyjson_mut_val* behavior = serializeBehavior(doc, &task->behaviors_ptr[index]);
        if (!yyjson_mut_arr_append(behaviors, behavior)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI task fields");
        }
    }
    return object;
}

/** @brief 人物 AI 事实归属 human.data；此分片仅保存人物 ID 及其任务和行为。 */
static yyjson_mut_val* serializeAI(yyjson_mut_doc* doc, const DominoHumanData* human_data) {
    if (human_data->task_count > DOMINO_HUMAN_TASK_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human AI task count exceeds capacity: human_id=%u count=%u", human_data->human_id,
                               human_data->task_count);
    }
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "human_id", human_data->human_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI record fields");
    }
    yyjson_mut_val* tasks = yyjson_mut_obj_add_arr(doc, object, "tasks");
    if (!tasks) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI record fields");
    }
    for (uint32_t index = 0; index < human_data->task_count; index++) {
        yyjson_mut_val* task = serializeTask(doc, dominoGetTaskByID(human_data->task_id_list[index]));
        if (!yyjson_mut_arr_append(tasks, task)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI record fields");
        }
    }
    return object;
}

/**
 * @brief 任务清单已由 human.data 恢复，此处只导入任务实体；已导入的行为块归 store。
 * 当前格式存档保证任务身份、归属与引用正确；本次资源失败直接终止，不回滚已导入内容。
 */
static void loadOneAI(yyjson_val* object) {
    domino_human_id_t human_id = (domino_human_id_t)yyjson_get_uint(yyjson_obj_get(object, "human_id"));
    yyjson_val* tasks = yyjson_obj_get(object, "tasks");
    if (yyjson_arr_size(tasks) > DOMINO_HUMAN_TASK_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human AI task count exceeds capacity: human_id=%u count=%zu", human_id,
                               yyjson_arr_size(tasks));
    }
    size_t current_task_count = kv_size(domino_all_task_list);
    size_t incoming_task_count = yyjson_arr_size(tasks);
    if (incoming_task_count > SIZE_MAX - current_task_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "Human AI task count overflows: current=%zu incoming=%zu", current_task_count,
                               incoming_task_count);
    }
    size_t required_task_count = current_task_count + incoming_task_count;
    const size_t maximum_task_capacity = SIZE_MAX / sizeof(DominoTask);
    if (required_task_count > maximum_task_capacity) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "Human AI task storage size overflows: count=%zu", required_task_count);
    }
    size_t task_index;
    size_t task_count;
    yyjson_val* task_object;
    yyjson_arr_foreach(tasks, task_index, task_count, task_object) {
        DominoTask task = {0};
        deserializeTask(task_object, &task);
        task.behaviors_ptr = calloc(task.behavior_count, sizeof(*task.behaviors_ptr));
        if (!task.behaviors_ptr) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI behaviors: task_id=%u count=%u", task.task_id, task.behavior_count);
        }
        yyjson_val* behaviors = yyjson_obj_get(task_object, "behaviors");
        for (uint32_t index = 0; index < task.behavior_count; index++) {
            deserializeBehavior(yyjson_arr_get(behaviors, index), &task.behaviors_ptr[index]);
        }
        DOMINO_CODE result = dominoHumanStoreImportTask(&task);
        if (result != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(result, "Cannot import human AI task: task_id=%u", task.task_id);
        }
    }
}

void serdeBindHumanAIReferences(void) {
    dominoHumanStoreResetRuntime();
}

void serdeSaveHumanAI(const char* save_path) {
    const size_t total_count = kv_size(domino_all_human_list);
    if (total_count > UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human AI record count exceeds storage limit: count=%zu", total_count);
    }
    uint32_t shard_count = storageEntityShardCount((uint32_t)total_count, DOMINO_STORAGE_SHARD_HUMAN_AI);
    for (uint32_t shard = 0; shard < shard_count; shard++) {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI JSON document: shard=%u", shard);
        }
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        if (!root) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI JSON root: shard=%u", shard);
        }
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_val* entities = yyjson_mut_obj_add_arr(doc, root, "entities");
        if (!entities || !yyjson_mut_obj_add_str(doc, root, "type", "human_ai") || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human AI shard fields: shard=%u", shard);
        }
        size_t start = (size_t)shard * DOMINO_STORAGE_SHARD_HUMAN_AI;
        size_t end = start + DOMINO_STORAGE_SHARD_HUMAN_AI;
        if (end > total_count) {
            end = total_count;
        }
        for (size_t index = start; index < end; index++) {
            yyjson_mut_val* object = serializeAI(doc, &kv_A(domino_all_human_data_list, index));
            if (!yyjson_mut_arr_append(entities, object)) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot serialize human AI record: human_id=%u",
                                       kv_A(domino_all_human_data_list, index).human_id);
            }
        }
        storageWriteShardFile("human_ai", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

void serdeLoadHumanAI(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    (void)total_count;
    for (uint32_t shard = 0; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human_ai", shard, save_path);
        yyjson_val* entities = yyjson_obj_get(yyjson_doc_get_root(doc), "entities");
        size_t index;
        size_t count;
        yyjson_val* object;
        yyjson_arr_foreach(entities, index, count, object) {
            loadOneAI(object);
        }
        yyjson_doc_free(doc);
    }
}
