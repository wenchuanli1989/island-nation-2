#include "meta.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../entry.h"
#include "../host/entry.h"
#include "../logger/entry.h"
#include "../time/entry.h"
#include "domino_shared_common.h"
#include "entry.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"
#include "sub_module.h"

#define STORAGE_META_FILE_NAME_MAX 64

static void buildDefaultMetaFileName(char* out, size_t out_size, const char* type_name) {
    (void)snprintf(out, out_size, "%s.meta.json", type_name);
}

/** @brief 写模块 meta 并登记摘要，根 meta 通过 type_name 引用模块文件。 */
static DOMINO_CODE writeOneModuleMetaFile(const char* save_path, const StorageModuleType* module_type, const char* meta_file_name,
                                          uint32_t total_count, uint32_t shard_count) {
    const char* type_name = module_type->type_name;
    char path[DOMINO_STORAGE_PATH_MAX];
    (void)snprintf(path, sizeof(path), "%s/%s", save_path, meta_file_name);
    bool do_verify = storageSubModuleIntegrityVerify();

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "type", type_name);
    yyjson_mut_obj_add_uint(doc, root, "total_count", total_count);
    yyjson_mut_obj_add_uint(doc, root, "shard_count", shard_count);

    if (module_type->write_meta_extra_fn) {
        DOMINO_CODE extra_result = module_type->write_meta_extra_fn(doc, root, type_name);
        if (extra_result != CODE_OK) {
            yyjson_mut_doc_free(doc);
            return extra_result;
        }
    }

    yyjson_mut_val* shard_digest_arr = yyjson_mut_obj_add_arr(doc, root, "shard");
    for (uint32_t shard_id = 0; shard_id < shard_count; shard_id++) {
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, shard_digest_arr);
        if (do_verify) {
            const StorageSubModuleShard* rec = storageSubModuleShardFind(type_name, shard_id);
            if (!rec) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "shard digest cache missing: %s shard=%u", type_name,
                                  shard_id);
                yyjson_mut_doc_free(doc);
                return ERR_INVALID_DATA;
            }
            yyjson_mut_obj_add_uint(doc, item, "crc32c", rec->crc32c);
            yyjson_mut_obj_add_uint(doc, item, "size", rec->size);
        } else {
            yyjson_mut_obj_add_uint(doc, item, "crc32c", 0);
            yyjson_mut_obj_add_uint(doc, item, "size", 0);
        }
    }

    uint32_t crc32c = 0;
    uint32_t size = 0;
    DOMINO_CODE result = storageWriteJsonFile(path, doc, do_verify, &crc32c, &size);
    yyjson_mut_doc_free(doc);
    if (result == CODE_OK) {
        StorageSubModuleMeta entity_sm = {0};
        strncpy(entity_sm.type_name, type_name, sizeof(entity_sm.type_name) - 1);
        entity_sm.total_count = total_count;
        entity_sm.shard_count = shard_count;
        entity_sm.crc32c = crc32c;
        entity_sm.size = size;
        result = storageSubModuleMetaAdd(entity_sm);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to index %s module meta", type_name);
        }
    } else {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "write %s meta file failed: %s", type_name,
                          dominoErrorCodeToString(result));
    }
    return result;
}

static void emitIncrementIds(yyjson_mut_doc* doc, yyjson_mut_val* inc) {
    /* ID 计数器独立持久化；road ID 由两端节点 ID 组合，无独立计数器。 */
    yyjson_mut_obj_add_uint(doc, inc, "human", atomic_load(&g_domino_global_increment_id.human_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "org", atomic_load(&g_domino_global_increment_id.org_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "country", atomic_load(&g_domino_global_increment_id.country_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "city", atomic_load(&g_domino_global_increment_id.city_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "island", atomic_load(&g_domino_global_increment_id.island_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "building", atomic_load(&g_domino_global_increment_id.building_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "fork_road", atomic_load(&g_domino_global_increment_id.fork_road_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "road_line", atomic_load(&g_domino_global_increment_id.road_line_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "asset", atomic_load(&g_domino_global_increment_id.asset_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "movable_object", atomic_load(&g_domino_global_increment_id.movable_object_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "name", atomic_load(&g_domino_global_increment_id.name_increment_id));
    yyjson_mut_obj_add_uint(doc, inc, "description", atomic_load(&g_domino_global_increment_id.description_increment_id));
}

static void ingestIncrementIds(yyjson_val* inc) {
    /* metaLoad 校验 inc 存在后再调用；缺失字段按 yyjson 默认 0 恢复。 */
    atomic_store(&g_domino_global_increment_id.human_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "human")));
    atomic_store(&g_domino_global_increment_id.org_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "org")));
    atomic_store(&g_domino_global_increment_id.country_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "country")));
    atomic_store(&g_domino_global_increment_id.city_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "city")));
    atomic_store(&g_domino_global_increment_id.island_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "island")));
    atomic_store(&g_domino_global_increment_id.building_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "building")));
    atomic_store(&g_domino_global_increment_id.fork_road_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "fork_road")));
    atomic_store(&g_domino_global_increment_id.road_line_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "road_line")));
    atomic_store(&g_domino_global_increment_id.asset_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "asset")));
    atomic_store(&g_domino_global_increment_id.movable_object_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "movable_object")));
    atomic_store(&g_domino_global_increment_id.name_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "name")));
    atomic_store(&g_domino_global_increment_id.description_increment_id, (uint32_t)yyjson_get_uint(yyjson_obj_get(inc, "description")));
}

DOMINO_CODE saveAllShards(const char* save_path) {
    DOMINO_CODE result = dominoMkdir(save_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "mkdir failed: %s", save_path);
        return result;
    }
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        DOMINO_CODE result = module_type->save_fn(save_path);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "save %s failed", module_type->type_name);
            return result;
        }
    }
    return CODE_OK;
}

DOMINO_CODE metaSave(const char* save_path) {
    char path[DOMINO_STORAGE_PATH_MAX];
    (void)snprintf(path, sizeof(path), "%s/meta.json", save_path);

    bool do_verify = storageSubModuleIntegrityVerify();
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        uint32_t total_count = (uint32_t)module_type->count_fn();
        uint32_t shard_count = storageModuleShardCount(module_type, total_count);
        char meta_file_name[STORAGE_META_FILE_NAME_MAX];
        buildDefaultMetaFileName(meta_file_name, sizeof(meta_file_name), module_type->type_name);
        DOMINO_CODE result = writeOneModuleMetaFile(save_path, module_type, meta_file_name, total_count, shard_count);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "write %s meta failed", module_type->type_name);
            return result;
        }
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_uint(doc, root, "version", DOMINO_STORAGE_SAVE_VERSION);
    yyjson_mut_obj_add_bool(doc, root, "integrity_verify", do_verify);

    yyjson_mut_val* info = yyjson_mut_obj_add_obj(doc, root, "info");
    yyjson_mut_obj_add_str(doc, info, "account_id", g_domino_storage_account_id);

    yyjson_mut_val* runtime_option = yyjson_mut_obj_add_obj(doc, root, "runtime_option");
    yyjson_mut_obj_add_str(doc, runtime_option, "run_mode", g_domino_storage_run_mode);

    yyjson_mut_val* runtime_data = yyjson_mut_obj_add_obj(doc, root, "runtime_data");
    yyjson_mut_obj_add_uint(doc, runtime_data, "game_date_time_ns", dominoGetRuntimeDateTimeBase());
    yyjson_mut_val* inc = yyjson_mut_obj_add_obj(doc, runtime_data, "global_increment_id");
    emitIncrementIds(doc, inc);

    yyjson_mut_val* sub_module = yyjson_mut_obj_add_obj(doc, root, "sub_module");
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        const StorageSubModuleMeta* module_sm = storageSubModuleMetaFind(module_type->type_name);
        if (!module_sm) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "module meta cache missing: %s", module_type->type_name);
            yyjson_mut_doc_free(doc);
            return ERR_INVALID_DATA;
        }
        yyjson_mut_val* module_obj = yyjson_mut_obj_add_obj(doc, sub_module, module_type->type_name);
        yyjson_mut_obj_add_uint(doc, module_obj, "crc32c", module_sm->crc32c);
        yyjson_mut_obj_add_uint(doc, module_obj, "size", module_sm->size);
    }

    uint32_t crc32c = 0;
    uint32_t size = 0;
    DOMINO_CODE result = storageWriteJsonFile(path, doc, do_verify, &crc32c, &size);
    yyjson_mut_doc_free(doc);
    return result;
}

/** @brief 先读取模块 meta 建立分片摘要索引，再加载实体分片。 */
static DOMINO_CODE loadSubModuleMeta(yyjson_val* sub_module, const StorageModuleType* module_type) {
    const char* type_name = module_type->type_name;
    bool do_verify = storageSubModuleIntegrityVerify();
    yyjson_val* item = yyjson_obj_get(sub_module, type_name);
    if (!item || !yyjson_is_obj(item)) {
        if (!module_type->required) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_INFO, "optional sub_module.%s missing, skip loading", type_name);
            return CODE_OK;
        }
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing sub_module.%s", type_name);
        return ERR_INVALID_JSON;
    }

    yyjson_val* crc_val = yyjson_obj_get(item, "crc32c");
    yyjson_val* size_val = yyjson_obj_get(item, "size");
    if (!crc_val || !yyjson_is_uint(crc_val) || !size_val || !yyjson_is_uint(size_val)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing crc32c/size for %s", type_name);
        return ERR_INVALID_JSON;
    }

    DOMINO_CODE result = CODE_OK;
    uint32_t meta_crc32c = 0;
    uint32_t meta_size = 0;
    if (do_verify) {
        meta_crc32c = (uint32_t)yyjson_get_uint(crc_val);
        meta_size = (uint32_t)yyjson_get_uint(size_val);
    }
    char meta_file_name[STORAGE_META_FILE_NAME_MAX];
    buildDefaultMetaFileName(meta_file_name, sizeof(meta_file_name), type_name);
    char module_meta_path[DOMINO_STORAGE_PATH_MAX];
    (void)snprintf(module_meta_path, sizeof(module_meta_path), "%s/%s", g_domino_engine_launch_config.storage_path, meta_file_name);
    yyjson_doc* doc = storageReadJsonFile(module_meta_path, do_verify, meta_crc32c, meta_size, &result);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to read sub_module meta file: %s", module_meta_path);
        return result;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid sub_module meta file: %s", module_meta_path);
        goto error;
    }
    yyjson_val* type_val = yyjson_obj_get(root, "type");
    const char* module_type_str = yyjson_get_str(type_val);
    size_t module_type_len = yyjson_get_len(type_val);
    if (!type_val || !yyjson_is_str(type_val) || !module_type_str || module_type_len != strlen(type_name) ||
        memcmp(module_type_str, type_name, module_type_len) != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid sub_module meta file: %s", module_meta_path);
        goto error;
    }
    yyjson_val* shard_arr = yyjson_obj_get(root, "shard");
    if (!shard_arr || !yyjson_is_arr(shard_arr)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: shard digest array mismatch", meta_file_name);
        goto error;
    }
    yyjson_val* shard_count_val = yyjson_obj_get(root, "shard_count");
    if (!shard_count_val || !yyjson_is_uint(shard_count_val)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: shard count missing", meta_file_name);
        goto error;
    }
    yyjson_val* total_count_val = yyjson_obj_get(root, "total_count");
    if (!total_count_val || !yyjson_is_uint(total_count_val)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: total count missing", meta_file_name);
        goto error;
    }
    uint32_t shard_count = (uint32_t)yyjson_get_uint(shard_count_val);
    if (shard_count != (uint32_t)yyjson_arr_size(shard_arr)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: shard count mismatch", meta_file_name);
        goto error;
    }
    uint32_t total_count = (uint32_t)yyjson_get_uint(total_count_val);
    uint32_t expected_shard_count = storageModuleShardCount(module_type, total_count);
    if (shard_count != expected_shard_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: shard count(%u) != expected(%u)", meta_file_name,
                          shard_count, expected_shard_count);
        goto error;
    }

    StorageSubModuleMeta load_sm = {0};
    strncpy(load_sm.type_name, type_name, sizeof(load_sm.type_name) - 1);
    load_sm.crc32c = meta_crc32c;
    load_sm.size = meta_size;
    load_sm.total_count = total_count;
    load_sm.shard_count = shard_count;
    result = storageSubModuleMetaAdd(load_sm);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to add sub_module meta for %s", type_name);
        yyjson_doc_free(doc);
        return result;
    }

    for (uint32_t shard_id = 0; shard_id < shard_count; shard_id++) {
        yyjson_val* shard_item = yyjson_arr_get(shard_arr, shard_id);
        if (!shard_item || !yyjson_is_obj(shard_item)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: shard digest item missing", meta_file_name);
            goto error;
        }
        yyjson_val* shard_crc_val = yyjson_obj_get(shard_item, "crc32c");
        yyjson_val* shard_size_val = yyjson_obj_get(shard_item, "size");
        if (!shard_crc_val || !yyjson_is_uint(shard_crc_val) || !shard_size_val || !yyjson_is_uint(shard_size_val)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: shard digest item missing crc32c/size",
                              meta_file_name);
            goto error;
        }
        uint32_t crc32c = (uint32_t)yyjson_get_uint(shard_crc_val);
        uint32_t size = (uint32_t)yyjson_get_uint(shard_size_val);
        StorageSubModuleShard shard_row = {0};
        strncpy(shard_row.type_name, type_name, sizeof(shard_row.type_name) - 1);
        shard_row.shard_id = shard_id;
        shard_row.crc32c = crc32c;
        shard_row.size = size;
        DOMINO_CODE result = storageSubModuleShardAdd(shard_row);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to add shard digest for %s shard=%u", type_name,
                              shard_id);
            yyjson_doc_free(doc);
            return result;
        }
    }

    if (module_type->read_meta_extra_fn) {
        result = module_type->read_meta_extra_fn(root, type_name);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to load %s module meta extra fields", type_name);
            yyjson_doc_free(doc);
            return result;
        }
    }

    yyjson_doc_free(doc);
    return CODE_OK;

error:
    yyjson_doc_free(doc);
    return ERR_INVALID_JSON;
}

DOMINO_CODE loadAllSubModule(yyjson_val* sub_module) {
    const char* save_path = g_domino_engine_launch_config.storage_path;

    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        DOMINO_CODE result = loadSubModuleMeta(sub_module, module_type);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to load %s meta", module_type->type_name);
            return result;
        }
    }

    /* 分片读取依赖已准备好的全部模块摘要。 */
    DOMINO_CODE result = CODE_OK;
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        const StorageSubModuleMeta* meta = storageSubModuleMetaFind(module_type->type_name);
        if (!meta || meta->shard_count == 0) {
            continue;
        }
        result = module_type->load_fn(save_path, meta->total_count, meta->shard_count);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to load %s", module_type->type_name);
            return result;
        }
    }

    /* 跨模块引用只能在所有实体主表和 ID map 都稳定后校验。校验阶段不得修改实体。 */
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        if (!module_type->validate_references_fn) {
            continue;
        }
        result = module_type->validate_references_fn();
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to validate %s references", module_type->type_name);
            return result;
        }
    }

    /* 所有引用均有效后再统一绑定；bind 回调只依赖已校验且不再变化的主表，不得失败。 */
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        if (module_type->bind_references_fn) {
            module_type->bind_references_fn();
        }
    }
    return CODE_OK;
}

yyjson_doc* metaLoad(DOMINO_CODE* result) {
    *result = CODE_OK;

    const char* save_path = g_domino_engine_launch_config.storage_path;
    char path[DOMINO_STORAGE_PATH_MAX];
    (void)snprintf(path, sizeof(path), "%s/meta.json", save_path);

    if (!dominoPathExists(path, false)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "meta.json not found: %s", path);
        *result = ERR_FILE_NOT_FOUND;
        return nullptr;
    }

    yyjson_doc* doc = storageReadJsonFile(path, false, 0, 0, result);
    if (*result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to read meta.json: %s", dominoErrorCodeToString(*result));
        return nullptr;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: root is not object");
        goto error;
    }
    yyjson_val* version_val = yyjson_obj_get(root, "version");
    if (!version_val || !yyjson_is_uint(version_val)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing version");
        goto error;
    }
    uint32_t version = (uint32_t)yyjson_get_uint(version_val);
    if (version != (uint32_t)DOMINO_STORAGE_SAVE_VERSION) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: version(%u) != expected(%u)", version,
                          (uint32_t)DOMINO_STORAGE_SAVE_VERSION);
        goto error;
    }
    yyjson_val* info = yyjson_obj_get(root, "info");
    if (!info || !yyjson_is_obj(info)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing info");
        goto error;
    }
    yyjson_val* runtime_option = yyjson_obj_get(root, "runtime_option");
    if (!runtime_option || !yyjson_is_obj(runtime_option)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing runtime_option");
        goto error;
    }
    yyjson_val* runtime_data = yyjson_obj_get(root, "runtime_data");
    if (!runtime_data || !yyjson_is_obj(runtime_data)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing runtime_data");
        goto error;
    }
    yyjson_val* game_date_time_ns_val = yyjson_obj_get(runtime_data, "game_date_time_ns");
    if (!game_date_time_ns_val || !yyjson_is_uint(game_date_time_ns_val)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing runtime_data.game_date_time_ns");
        goto error;
    }
    yyjson_val* inc = yyjson_obj_get(runtime_data, "global_increment_id");
    if (!inc || !yyjson_is_obj(inc)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                              "invalid meta.json: missing runtime_data.global_increment_id");
        goto error;
    }

    yyjson_val* sub_module = yyjson_obj_get(root, "sub_module");
    if (!sub_module || !yyjson_is_obj(sub_module)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing sub_module");
        goto error;
    }

    yyjson_val* integrity_verify_val = yyjson_obj_get(root, "integrity_verify");
    if (!integrity_verify_val || !yyjson_is_bool(integrity_verify_val)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: missing integrity_verify");
        goto error;
    }
    storageSubModuleSetIntegrityVerify(yyjson_get_bool(integrity_verify_val));

    serdeReadStr(info, "account_id", g_domino_storage_account_id, sizeof(g_domino_storage_account_id));
    serdeReadStr(runtime_option, "run_mode", g_domino_storage_run_mode, sizeof(g_domino_storage_run_mode));
    dominoSetRuntimeDateTimeBase(yyjson_get_uint(game_date_time_ns_val));
    ingestIncrementIds(inc);
    return doc;
error:
    *result = ERR_INVALID_JSON;
    yyjson_doc_free(doc);
    return nullptr;
}
