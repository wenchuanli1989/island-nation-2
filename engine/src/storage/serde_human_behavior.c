#include "../human/store.h"
#include "domino_shared_common.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/* 只解码当前格式的可信事实；实体容量与索引资源由导入入口检查。 */
#define BEHAVIOR_READ_UINT(object, record, field) ((record)->field = (typeof((record)->field))yyjson_get_uint(yyjson_obj_get(object, #field)))

static void deserializeBehavior(yyjson_val* object, DominoBehavior* behavior_ptr) {
    BEHAVIOR_READ_UINT(object, behavior_ptr, behavior_id);
    BEHAVIOR_READ_UINT(object, behavior_ptr, type);
    BEHAVIOR_READ_UINT(object, behavior_ptr, planned_begin_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, planned_end_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, started_at_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, ended_at_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, accounted_at_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, active_elapsed_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, status);
    behavior_ptr->started_flag = yyjson_get_bool(yyjson_obj_get(object, "started_flag"));
}

#undef BEHAVIOR_READ_UINT

/** @brief 独立写入全局行为事实；睡眠目标时长由计划区间推导。 */
static yyjson_mut_val* serializeBehavior(yyjson_mut_doc* doc, const DominoBehavior* behavior_ptr) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "behavior_id", behavior_ptr->behavior_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "type", behavior_ptr->type) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_begin_ms", behavior_ptr->planned_begin_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "planned_end_ms", behavior_ptr->planned_end_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "started_at_ms", behavior_ptr->started_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "ended_at_ms", behavior_ptr->ended_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "accounted_at_ms", behavior_ptr->accounted_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "active_elapsed_ms", behavior_ptr->active_elapsed_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "status", behavior_ptr->status) ||
        !yyjson_mut_obj_add_bool(doc, object, "started_flag", behavior_ptr->started_flag)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human behavior fields");
    }
    return object;
}

void serdeSaveHumanBehavior(const char* save_path) {
    const size_t total_count = kv_size(domino_all_behavior_list);
    if (total_count > UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human behavior count exceeds storage limit: count=%zu", total_count);
    }
    const uint32_t shard_count = storageEntityShardCount((uint32_t)total_count, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA);
    for (uint32_t shard = 0U; shard < shard_count; shard++) {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human behavior JSON document: shard=%u", shard);
        }
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        if (!root) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human behavior JSON root: shard=%u", shard);
        }
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_val* entities = yyjson_mut_obj_add_arr(doc, root, "entities");
        if (!entities || !yyjson_mut_obj_add_str(doc, root, "type", "human_behavior") || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human behavior shard fields: shard=%u", shard);
        }
        const size_t start = (size_t)shard * DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA;
        const size_t remaining_count = total_count - start;
        const size_t count = remaining_count < DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA ? remaining_count : DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA;
        for (size_t index = start; index < start + count; index++) {
            yyjson_mut_val* object = serializeBehavior(doc, &kv_A(domino_all_behavior_list, index));
            if (!yyjson_mut_arr_append(entities, object)) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot serialize human behavior record: behavior_id=%u",
                                       kv_A(domino_all_behavior_list, index).behavior_id);
            }
        }
        storageWriteShardFile("human_behavior", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

void serdeLoadHumanBehavior(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    storageValidateLoadCount(total_count, sizeof(DominoBehavior));
    for (uint32_t shard = 0U; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human_behavior", shard, save_path);
        yyjson_val* entities = yyjson_obj_get(yyjson_doc_get_root(doc), "entities");
        size_t index;
        size_t count;
        yyjson_val* object;
        yyjson_arr_foreach(entities, index, count, object) {
            DominoBehavior behavior = {0};
            deserializeBehavior(object, &behavior);
            const DOMINO_CODE code = dominoHumanStoreImportBehavior(&behavior);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "Cannot import human behavior: behavior_id=%u", behavior.behavior_id);
            }
        }
        yyjson_doc_free(doc);
    }
}
