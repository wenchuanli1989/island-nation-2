#include "../human/motivation/entry.h"
#include "../human/state.h"
#include "../human/store.h"
#include "domino_shared_common.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/** @file serde_human_motivation.c
 * @brief 动机记录：身份、归属、类型、生命周期（终态保存在 status 中）、需求轮次与承接任务、累计实施次数、最近失败原因及最近 10 条成功明细
 * （按时间顺序）。回收后的动机不再保存；待评估标志与下次规划时刻属于本会话运行态，不保存。
 */

static yyjson_mut_val* serializeMotivation(yyjson_mut_doc* doc, const DominoMotivation* motivation_ptr) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "motivation_id", motivation_ptr->motivation_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "owner_human_id", motivation_ptr->owner_human_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "type", motivation_ptr->type) ||
        !yyjson_mut_obj_add_uint(doc, object, "status", motivation_ptr->status) ||
        !yyjson_mut_obj_add_uint(doc, object, "need_round", motivation_ptr->need_round) ||
        !yyjson_mut_obj_add_uint(doc, object, "round_task_id", motivation_ptr->round_task_id) ||
        !yyjson_mut_obj_add_uint(doc, object, "success_count", motivation_ptr->success_count) ||
        !yyjson_mut_obj_add_uint(doc, object, "last_failure_reason", motivation_ptr->last_failure_reason)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human motivation fields");
    }
    yyjson_mut_val* history = yyjson_mut_obj_add_arr(doc, object, "history_list");
    if (!history) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human motivation fields");
    }
    for (uint8_t index = 0U; index < motivation_ptr->history_count; index++) {
        const DominoMotivationHistory entry = dominoHumanMotivationHistoryAt(motivation_ptr, index);
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, history);
        if (!item || !yyjson_mut_obj_add_uint(doc, item, "task_id", entry.task_id) ||
            !yyjson_mut_obj_add_uint(doc, item, "started_at_ms", entry.started_at_ms) ||
            !yyjson_mut_obj_add_uint(doc, item, "ended_at_ms", entry.ended_at_ms)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human motivation fields");
        }
    }
    return object;
}

/** @brief 解码可信动机；明细按时间顺序恢复为从 0 开始的环形布局。 */
static void deserializeMotivation(yyjson_val* object, DominoMotivation* motivation_ptr) {
    motivation_ptr->motivation_id = (domino_motivation_id_t)yyjson_get_uint(yyjson_obj_get(object, "motivation_id"));
    motivation_ptr->owner_human_id = (domino_human_id_t)yyjson_get_uint(yyjson_obj_get(object, "owner_human_id"));
    motivation_ptr->type = (uint8_t)yyjson_get_uint(yyjson_obj_get(object, "type"));
    motivation_ptr->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(object, "status"));
    motivation_ptr->need_round = (uint32_t)yyjson_get_uint(yyjson_obj_get(object, "need_round"));
    motivation_ptr->round_task_id = (domino_task_id_t)yyjson_get_uint(yyjson_obj_get(object, "round_task_id"));
    motivation_ptr->success_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(object, "success_count"));
    motivation_ptr->last_failure_reason = (uint8_t)yyjson_get_uint(yyjson_obj_get(object, "last_failure_reason"));
    yyjson_val* history = yyjson_obj_get(object, "history_list");
    const size_t count = yyjson_arr_size(history);
    if (count > DOMINO_MOTIVATION_HISTORY_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Motivation history exceeds capacity: motivation_id=%u", motivation_ptr->motivation_id);
    }
    for (size_t index = 0; index < count; index++) {
        yyjson_val* item = yyjson_arr_get(history, index);
        motivation_ptr->history_list[index] = (DominoMotivationHistory){
            .task_id = (domino_task_id_t)yyjson_get_uint(yyjson_obj_get(item, "task_id")),
            .started_at_ms = (domino_runtime_ms_t)yyjson_get_uint(yyjson_obj_get(item, "started_at_ms")),
            .ended_at_ms = (domino_runtime_ms_t)yyjson_get_uint(yyjson_obj_get(item, "ended_at_ms")),
        };
    }
    motivation_ptr->history_count = (uint8_t)count;
    motivation_ptr->history_head_index = 0U;
}

void serdeSaveHumanMotivation(const char* save_path) {
    const size_t total_count = kv_size(g_domino_motivation_list);
    if (total_count > UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human motivation count exceeds storage limit: count=%zu", total_count);
    }
    const uint32_t shard_count = storageEntityShardCount((uint32_t)total_count, DOMINO_STORAGE_SHARD_HUMAN_TASK);
    for (uint32_t shard = 0U; shard < shard_count; shard++) {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human motivation JSON document: shard=%u", shard);
        }
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        if (!root) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human motivation JSON root: shard=%u", shard);
        }
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_val* entities = yyjson_mut_obj_add_arr(doc, root, "entities");
        if (!entities || !yyjson_mut_obj_add_str(doc, root, "type", "human_motivation") || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human motivation shard fields: shard=%u", shard);
        }
        const size_t start = (size_t)shard * DOMINO_STORAGE_SHARD_HUMAN_TASK;
        const size_t remaining_count = total_count - start;
        const size_t count = remaining_count < DOMINO_STORAGE_SHARD_HUMAN_TASK ? remaining_count : DOMINO_STORAGE_SHARD_HUMAN_TASK;
        for (size_t index = start; index < start + count; index++) {
            if (!yyjson_mut_arr_append(entities, serializeMotivation(doc, &kv_A(g_domino_motivation_list, index)))) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot serialize human motivation record: motivation_id=%u",
                                       kv_A(g_domino_motivation_list, index).motivation_id);
            }
        }
        storageWriteShardFile("human_motivation", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

void serdeLoadHumanMotivation(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    storageValidateLoadCount(total_count, sizeof(DominoMotivation));
    for (uint32_t shard = 0U; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human_motivation", shard, save_path);
        yyjson_val* entities = yyjson_obj_get(yyjson_doc_get_root(doc), "entities");
        size_t index;
        size_t count;
        yyjson_val* object;
        yyjson_arr_foreach(entities, index, count, object) {
            DominoMotivation motivation = {0};
            deserializeMotivation(object, &motivation);
            const DOMINO_CODE code = dominoHumanStoreImportMotivation(&motivation);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "Cannot import human motivation: motivation_id=%u", motivation.motivation_id);
            }
        }
        yyjson_doc_free(doc);
    }
}
