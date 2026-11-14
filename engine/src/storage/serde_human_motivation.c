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
static void deserializeMotivation(yyjson_val* object, DominoMotivation* value) {
    READ(object, value, motivation_id);
    READ(object, value, owner_human_id);
    READ(object, value, type);
    READ(object, value, status);
    READ(object, value, implementation_count);
    READ(object, value, work_round);
    READ(object, value, closed_round);
    READ(object, value, needs_action);
    READ(object, value, reassess);
    READ(object, value, failed_work);
    READ(object, value, last_failure_reason);
    readCheck(yyjson_obj_get(object, "retry"), &value->retry);
    yyjson_val* history = yyjson_obj_get(object, "history");
    size_t count = yyjson_arr_size(history);
    if (count > DOMINO_MOTIVATION_HISTORY_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "motivation history capacity");
    value->history_count = (uint8_t)count;
    for (size_t i = 0; i < count; ++i) {
        yyjson_val* row = yyjson_arr_get(history, i);
        READ(row, &value->history[i], task_id);
        READ(row, &value->history[i], started_at_ms);
        READ(row, &value->history[i], ended_at_ms);
    }
}
static yyjson_mut_val* serializeMotivation(yyjson_mut_doc* doc, const DominoMotivation* value) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot serialize motivation");
    WRITE(object, value, motivation_id);
    WRITE(object, value, owner_human_id);
    WRITE(object, value, type);
    WRITE(object, value, status);
    WRITE(object, value, implementation_count);
    WRITE(object, value, work_round);
    WRITE(object, value, closed_round);
    WRITE(object, value, needs_action);
    WRITE(object, value, reassess);
    WRITE(object, value, failed_work);
    WRITE(object, value, last_failure_reason);
    if (!yyjson_mut_obj_add_val(doc, object, "retry", writeCheck(doc, value->retry)))
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "retry field");
    yyjson_mut_val* history = yyjson_mut_obj_add_arr(doc, object, "history");
    if (!history)
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "history field");
    for (uint8_t i = 0; i < value->history_count; ++i) {
        yyjson_mut_val* row = yyjson_mut_obj(doc);
        if (!row || !yyjson_mut_arr_append(history, row))
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "history row");
        WRITE(row, &value->history[i], task_id);
        WRITE(row, &value->history[i], started_at_ms);
        WRITE(row, &value->history[i], ended_at_ms);
    }
    return object;
}
void serdeSaveHumanMotivation(const char* save_path) {
    const size_t total_count = kv_size(domino_all_motivation_list);
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
        if (!entities || !yyjson_mut_obj_add_str(doc, root, "type", "human_motivation") || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human task shard fields: shard=%u", shard);
        }
        const size_t start = (size_t)shard * DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA;
        const size_t remaining_count = total_count - start;
        const size_t count = remaining_count < DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA ? remaining_count : DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA;
        for (size_t index = start; index < start + count; index++) {
            yyjson_mut_val* object = serializeMotivation(doc, &kv_A(domino_all_motivation_list, index));
            if (!yyjson_mut_arr_append(entities, object)) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot serialize human task record: motivation_id=%u",
                                       kv_A(domino_all_motivation_list, index).motivation_id);
            }
        }
        storageWriteShardFile("human_motivation", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

/** @brief 按全局顺序直接调用 human store 导入可信任务并建立 ID 索引；人物任务清单由 human.data 恢复，不在此重建。 */
void serdeLoadHumanMotivation(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    storageValidateLoadCount(total_count, sizeof(DominoMotivation));
    for (uint32_t shard = 0U; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human_motivation", shard, save_path);
        yyjson_val* entities = yyjson_obj_get(yyjson_doc_get_root(doc), "entities");
        size_t index;
        size_t count;
        yyjson_val* object;
        yyjson_arr_foreach(entities, index, count, object) {
            DominoMotivation task = {0};
            deserializeMotivation(object, &task);
            const DOMINO_CODE code = dominoHumanStoreImportMotivation(&task);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "Cannot import human task: motivation_id=%u", task.motivation_id);
            }
        }
        yyjson_doc_free(doc);
    }
}
