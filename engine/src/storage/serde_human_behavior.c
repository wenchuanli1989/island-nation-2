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
static void deserializeBehavior(yyjson_val* object, DominoBehavior* value) {
    READ(object, value, behavior_id);
    READ(object, value, task_id);
    READ(object, value, next_record_id);
    READ(object, value, started_at_ms);
    READ(object, value, ended_at_ms);
    READ(object, value, active_elapsed_ms);
    READ(object, value, effect_phase_ms);
    READ(object, value, status);
    yyjson_val* plan = yyjson_obj_get(object, "plan");
    READ(plan, &value->plan, duration_ms);
    READ(plan, &value->plan, type);
}
static yyjson_mut_val* serializeBehavior(yyjson_mut_doc* doc, const DominoBehavior* value) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot serialize behavior");
    WRITE(object, value, behavior_id);
    WRITE(object, value, task_id);
    WRITE(object, value, next_record_id);
    WRITE(object, value, started_at_ms);
    WRITE(object, value, ended_at_ms);
    WRITE(object, value, active_elapsed_ms);
    WRITE(object, value, effect_phase_ms);
    WRITE(object, value, status);
    yyjson_mut_val* plan = yyjson_mut_obj_add_obj(doc, object, "plan");
    if (!plan)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "plan fields");
    WRITE(plan, &value->plan, duration_ms);
    WRITE(plan, &value->plan, type);
    return object;
}
void serdeSaveHumanBehavior(const char* save_path) {
    const size_t total_count = kv_size(domino_all_behavior_list);
    if (total_count > UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human task count exceeds storage limit: count=%zu", total_count);
    }
    const uint32_t shard_count = storageEntityShardCount((uint32_t)total_count, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA);
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
        if (!entities || !yyjson_mut_obj_add_str(doc, root, "type", "human_behavior") || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task shard fields: shard=%u", shard);
        }
        const size_t start = (size_t)shard * DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA;
        const size_t remaining_count = total_count - start;
        const size_t count = remaining_count < DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA ? remaining_count : DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA;
        for (size_t index = start; index < start + count; index++) {
            yyjson_mut_val* object = serializeBehavior(doc, &kv_A(domino_all_behavior_list, index));
            if (!yyjson_mut_arr_append(entities, object)) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot serialize human task record: behavior_id=%u",
                                       kv_A(domino_all_behavior_list, index).behavior_id);
            }
        }
        storageWriteShardFile("human_behavior", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

/** @brief 按全局顺序直接调用 human store 导入可信任务并建立 ID 索引；人物任务清单由 human.data 恢复，不在此重建。 */
void serdeLoadHumanBehavior(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    storageValidateLoadCount(total_count, sizeof(DominoBehavior));
    for (uint32_t shard = 0U; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human_behavior", shard, save_path);
        yyjson_val* entities = yyjson_obj_get(yyjson_doc_get_root(doc), "entities");
        size_t index;
        size_t count;
        yyjson_val* object;
        yyjson_arr_foreach(entities, index, count, object) {
            DominoBehavior task = {0};
            deserializeBehavior(object, &task);
            const DOMINO_CODE code = dominoHumanStoreImportBehavior(&task);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "Cannot import human task: behavior_id=%u", task.behavior_id);
            }
        }
        yyjson_doc_free(doc);
    }
}
