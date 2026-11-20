#include "../human/state.h"
#include "../human/store.h"
#include "domino_shared_common.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/** @file serde_human_behavior.c
 * @brief 行为执行记录：正式 ID、所属任务、步骤序号、通道、标志、行为依赖、执行参数、状态、实际时刻、有效进度和结束原因；
 * 周期效果相位由有效进度推导，不保存。
 * @note 终态记录只保留已提交明细，所属任务的必要事实已在终态提交时归并；不保存结算游标或待归并标记。
 */

#define BEHAVIOR_READ_UINT(object, record, field) ((record)->field = (typeof((record)->field))yyjson_get_uint(yyjson_obj_get(object, #field)))

static void deserializeBehavior(yyjson_val* object, DominoBehavior* behavior_ptr) {
    BEHAVIOR_READ_UINT(object, behavior_ptr, behavior_id);
    BEHAVIOR_READ_UINT(object, behavior_ptr, task_id);
    BEHAVIOR_READ_UINT(object, behavior_ptr, type);
    BEHAVIOR_READ_UINT(object, behavior_ptr, status);
    BEHAVIOR_READ_UINT(object, behavior_ptr, started_at_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, ended_at_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, target_effective_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, effective_progress_ms);
    BEHAVIOR_READ_UINT(object, behavior_ptr, param);
    BEHAVIOR_READ_UINT(object, behavior_ptr, end_reason);
    BEHAVIOR_READ_UINT(object, behavior_ptr, step_seq);
    BEHAVIOR_READ_UINT(object, behavior_ptr, channel_index);
    BEHAVIOR_READ_UINT(object, behavior_ptr, flags);
    yyjson_val* array = yyjson_obj_get(object, "dependency_list");
    const size_t count = yyjson_arr_size(array);
    if (count > DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human behavior dependency count exceeds capacity: behavior_id=%u", behavior_ptr->behavior_id);
    }
    for (size_t index = 0; index < count; index++) {
        yyjson_val* item = yyjson_arr_get(array, index);
        behavior_ptr->dependency_list[index] = (DominoBehaviorDependency){
            .step_seq = (uint32_t)yyjson_get_uint(yyjson_obj_get(item, "step_seq")),
            .kind = (uint8_t)yyjson_get_uint(yyjson_obj_get(item, "kind")),
            .state = (uint8_t)yyjson_get_uint(yyjson_obj_get(item, "state")),
        };
    }
}

#undef BEHAVIOR_READ_UINT

static yyjson_mut_val* serializeBehavior(yyjson_mut_doc* doc, const DominoBehavior* behavior_ptr) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "behavior_id", behavior_ptr->behavior_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "task_id", behavior_ptr->task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "type", behavior_ptr->type) || !yyjson_mut_obj_add_uint(doc, object, "status", behavior_ptr->status) ||
        !yyjson_mut_obj_add_uint(doc, object, "started_at_ms", behavior_ptr->started_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "ended_at_ms", behavior_ptr->ended_at_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "target_effective_ms", behavior_ptr->target_effective_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "effective_progress_ms", behavior_ptr->effective_progress_ms) ||
        !yyjson_mut_obj_add_uint(doc, object, "param", behavior_ptr->param) ||
        !yyjson_mut_obj_add_uint(doc, object, "end_reason", behavior_ptr->end_reason) ||
        !yyjson_mut_obj_add_uint(doc, object, "step_seq", behavior_ptr->step_seq) ||
        !yyjson_mut_obj_add_uint(doc, object, "channel_index", behavior_ptr->channel_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "flags", behavior_ptr->flags)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human behavior fields");
    }
    yyjson_mut_val* array = yyjson_mut_obj_add_arr(doc, object, "dependency_list");
    if (!array) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human behavior fields");
    }
    for (uint8_t index = 0U; index < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; index++) {
        const DominoBehaviorDependency* dependency_ptr = &behavior_ptr->dependency_list[index];
        if (dependency_ptr->kind == DOMINO_BEHAVIOR_DEPENDENCY_NONE) {
            continue;
        }
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, array);
        if (!item || !yyjson_mut_obj_add_uint(doc, item, "step_seq", dependency_ptr->step_seq) ||
            !yyjson_mut_obj_add_uint(doc, item, "kind", dependency_ptr->kind) ||
            !yyjson_mut_obj_add_uint(doc, item, "state", dependency_ptr->state)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human behavior fields");
        }
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
            if (!yyjson_mut_arr_append(entities, serializeBehavior(doc, &kv_A(domino_all_behavior_list, index)))) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot serialize human behavior record: behavior_id=%u",
                                       kv_A(domino_all_behavior_list, index).behavior_id);
            }
        }
        storageWriteShardFile("human_behavior", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

/** @brief 直接调用 human store 导入可信行为，保留身份、进度和业务相位；不重放终态归并。 */
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
