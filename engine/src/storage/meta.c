#define _POSIX_C_SOURCE 200809L

#include "meta.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

/** @brief 分片已全部写入后，写模块 meta 并直接返回根 meta 所需摘要。 */
static DOMINO_CODE writeOneModuleMetaFile(const char* save_path, const StorageModuleType* module_type, uint32_t* crc32c_out, uint32_t* size_out) {
    const char* type_name = module_type->type_name;
    uint32_t total_count = (uint32_t)module_type->count_fn();
    uint32_t shard_count = storageModuleShardCount(module_type, total_count);
    char path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s.meta.json", save_path, type_name);
    if (written < 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to format module metadata path (module=%s)", type_name);
        return ERR_STRING_FORMAT;
    }
    if ((size_t)written >= sizeof(path)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "module metadata path exceeds buffer (capacity=%zu)",
                          sizeof(path));
        return ERR_BUFFER_TOO_SMALL;
    }
    bool do_verify = storageSubModuleIntegrityVerify();

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate module metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    if (!root) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate module metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }
    yyjson_mut_doc_set_root(doc, root);

    if (!yyjson_mut_obj_add_str(doc, root, "type", type_name) || !yyjson_mut_obj_add_uint(doc, root, "total_count", total_count) ||
        !yyjson_mut_obj_add_uint(doc, root, "shard_count", shard_count)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate module metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }

    if (module_type->write_meta_extra_fn) {
        DOMINO_CODE extra_result = module_type->write_meta_extra_fn(doc, root, type_name);
        if (extra_result != CODE_OK) {
            return extra_result;
        }
    }

    yyjson_mut_val* shard_digest_arr = yyjson_mut_obj_add_arr(doc, root, "shard");
    if (!shard_digest_arr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate module metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }
    for (uint32_t shard_id = 0; shard_id < shard_count; shard_id++) {
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, shard_digest_arr);
        if (!item) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate module metadata JSON (path=%s)", path);
            return ERR_MEMORY_ALLOC;
        }
        uint32_t shard_crc32c = 0;
        uint32_t shard_size = 0;
        if (do_verify) {
            /* saveAllShards 已按同一注册表及连续编号登记全部摘要。 */
            const StorageSubModuleShard* rec = storageSubModuleShardFind(type_name, shard_id);
            shard_crc32c = rec->crc32c;
            shard_size = rec->size;
        }
        if (!yyjson_mut_obj_add_uint(doc, item, "crc32c", shard_crc32c) || !yyjson_mut_obj_add_uint(doc, item, "size", shard_size)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate module metadata JSON (path=%s)", path);
            return ERR_MEMORY_ALLOC;
        }
    }

    DOMINO_CODE result = storageWriteJsonFile(path, doc, do_verify, crc32c_out, size_out);
    if (result != CODE_OK) {
        return result;
    }
    yyjson_mut_doc_free(doc);
    return CODE_OK;
}

static bool emitIncrementIds(yyjson_mut_doc* doc, yyjson_mut_val* inc) {
    /* ID 计数器独立持久化；road ID 由两端节点 ID 组合，无独立计数器。 */
    return yyjson_mut_obj_add_uint(doc, inc, "human", atomic_load(&g_domino_global_increment_id.human_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "task", atomic_load(&g_domino_global_increment_id.task_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "org", atomic_load(&g_domino_global_increment_id.org_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "country", atomic_load(&g_domino_global_increment_id.country_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "city", atomic_load(&g_domino_global_increment_id.city_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "island", atomic_load(&g_domino_global_increment_id.island_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "building", atomic_load(&g_domino_global_increment_id.building_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "fork_road", atomic_load(&g_domino_global_increment_id.fork_road_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "road_line", atomic_load(&g_domino_global_increment_id.road_line_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "asset", atomic_load(&g_domino_global_increment_id.asset_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "movable_object", atomic_load(&g_domino_global_increment_id.movable_object_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "name", atomic_load(&g_domino_global_increment_id.name_increment_id)) &&
           yyjson_mut_obj_add_uint(doc, inc, "description", atomic_load(&g_domino_global_increment_id.description_increment_id));
}

static DOMINO_CODE ingestIncrementIds(yyjson_val* inc) {
    yyjson_val* task_val = yyjson_obj_get(inc, "task");
    if (!yyjson_is_uint(task_val) || yyjson_get_uint(task_val) > UINT32_MAX) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                              "metadata runtime_data.global_increment_id.task must be a uint32 value");
        return ERR_INVALID_DATA;
    }
    /* 游标为下一候选 ID；0 是初始化或回绕后的合法值，分配时跳过。 */
    atomic_store(&g_domino_global_increment_id.task_increment_id, (domino_task_id_t)yyjson_get_uint(task_val));
    /* 其他实体仍按各模块原有计数器格式恢复。 */
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
    return CODE_OK;
}

DOMINO_CODE saveAllShards(const char* save_path) {
    if (dominoMkdir(save_path) != 0) {
        int mkdir_errno = errno;
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to create save directory (path=%s, errno=%d)", save_path,
                          mkdir_errno);
        return ERR_DIRECTORY_CREATE;
    }
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        DOMINO_CODE result = module_type->save_fn(save_path);
        if (result != CODE_OK) {
            return result;
        }
    }
    return CODE_OK;
}

DOMINO_CODE metaSave(const char* save_path) {
    char path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/meta.json", save_path);
    if (written < 0) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to format root metadata path");
        return ERR_STRING_FORMAT;
    }
    if ((size_t)written >= sizeof(path)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "root metadata path exceeds buffer (capacity=%zu)", sizeof(path));
        return ERR_BUFFER_TOO_SMALL;
    }

    bool do_verify = storageSubModuleIntegrityVerify();
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    if (!root) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }
    yyjson_mut_doc_set_root(doc, root);

    if (!yyjson_mut_obj_add_uint(doc, root, "version", DOMINO_STORAGE_SAVE_VERSION) ||
        !yyjson_mut_obj_add_bool(doc, root, "integrity_verify", do_verify)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }

    yyjson_mut_val* info = yyjson_mut_obj_add_obj(doc, root, "info");
    if (!info || !yyjson_mut_obj_add_str(doc, info, "account_id", g_domino_storage_account_id)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }

    yyjson_mut_val* runtime_option = yyjson_mut_obj_add_obj(doc, root, "runtime_option");
    if (!runtime_option || !yyjson_mut_obj_add_str(doc, runtime_option, "run_mode", g_domino_storage_run_mode)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }

    yyjson_mut_val* runtime_data = yyjson_mut_obj_add_obj(doc, root, "runtime_data");
    if (!runtime_data || !yyjson_mut_obj_add_uint(doc, runtime_data, "game_date_time_ns", dominoGetRuntimeDateTimeBase())) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }
    yyjson_mut_val* inc = yyjson_mut_obj_add_obj(doc, runtime_data, "global_increment_id");
    if (!inc || !emitIncrementIds(doc, inc)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }

    yyjson_mut_val* sub_module = yyjson_mut_obj_add_obj(doc, root, "sub_module");
    if (!sub_module) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
        return ERR_MEMORY_ALLOC;
    }
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        uint32_t crc32c = 0;
        uint32_t size = 0;
        DOMINO_CODE result = writeOneModuleMetaFile(save_path, module_type, &crc32c, &size);
        if (result != CODE_OK) {
            return result;
        }
        yyjson_mut_val* module_obj = yyjson_mut_obj_add_obj(doc, sub_module, module_type->type_name);
        if (!module_obj || !yyjson_mut_obj_add_uint(doc, module_obj, "crc32c", crc32c) || !yyjson_mut_obj_add_uint(doc, module_obj, "size", size)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to allocate root metadata JSON (path=%s)", path);
            return ERR_MEMORY_ALLOC;
        }
    }

    DOMINO_CODE result = storageWriteJsonFile(path, doc, false, nullptr, nullptr);
    if (result != CODE_OK) {
        return result;
    }
    yyjson_mut_doc_free(doc);
    return CODE_OK;
}

/** @brief 先读取模块 meta 建立分片摘要索引，再加载实体分片。 */
static DOMINO_CODE loadSubModuleMeta(yyjson_val* sub_module, const StorageModuleType* module_type) {
    const char* type_name = module_type->type_name;
    bool do_verify = storageSubModuleIntegrityVerify();
    yyjson_val* item = yyjson_obj_get(sub_module, type_name);
    if (!item && !module_type->required) {
        return CODE_OK;
    }
    if (!yyjson_is_obj(item)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "root metadata module entry must be an object (module=%s)",
                          type_name);
        return ERR_INVALID_DATA;
    }

    yyjson_val* crc_val = yyjson_obj_get(item, "crc32c");
    yyjson_val* size_val = yyjson_obj_get(item, "size");
    if (!yyjson_is_uint(crc_val) || yyjson_get_uint(crc_val) > UINT32_MAX || !yyjson_is_uint(size_val) || yyjson_get_uint(size_val) > UINT32_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                          "root metadata module crc32c and size must be uint32 values (module=%s)", type_name);
        return ERR_INVALID_DATA;
    }

    DOMINO_CODE result = CODE_OK;
    uint32_t meta_crc32c = 0;
    uint32_t meta_size = 0;
    if (do_verify) {
        meta_crc32c = (uint32_t)yyjson_get_uint(crc_val);
        meta_size = (uint32_t)yyjson_get_uint(size_val);
    }
    char module_meta_path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(module_meta_path, sizeof(module_meta_path), "%s/%s.meta.json", g_domino_engine_launch_config.storage_path, type_name);
    if (written < 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to format module metadata path (module=%s)", type_name);
        return ERR_STRING_FORMAT;
    }
    if ((size_t)written >= sizeof(module_meta_path)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "module metadata path exceeds buffer (module=%s, capacity=%zu)",
                          type_name, sizeof(module_meta_path));
        return ERR_BUFFER_TOO_SMALL;
    }
    yyjson_doc* doc = storageReadJsonFile(module_meta_path, do_verify, meta_crc32c, meta_size, &result);
    if (result != CODE_OK) {
        return result;
    }
    const char* invalid_reason = nullptr;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        invalid_reason = "root must be an object";
        goto invalid_data;
    }
    yyjson_val* type_val = yyjson_obj_get(root, "type");
    const char* module_type_str = yyjson_get_str(type_val);
    size_t module_type_len = yyjson_get_len(type_val);
    if (!yyjson_is_str(type_val) || module_type_len != strlen(type_name) || memcmp(module_type_str, type_name, module_type_len) != 0) {
        invalid_reason = "field type does not match module name";
        goto invalid_data;
    }
    yyjson_val* shard_arr = yyjson_obj_get(root, "shard");
    if (!yyjson_is_arr(shard_arr)) {
        invalid_reason = "field shard must be an array";
        goto invalid_data;
    }
    yyjson_val* shard_count_val = yyjson_obj_get(root, "shard_count");
    if (!yyjson_is_uint(shard_count_val) || yyjson_get_uint(shard_count_val) > UINT32_MAX) {
        invalid_reason = "field shard_count must be a uint32 value";
        goto invalid_data;
    }
    yyjson_val* total_count_val = yyjson_obj_get(root, "total_count");
    if (!yyjson_is_uint(total_count_val) || yyjson_get_uint(total_count_val) > UINT32_MAX) {
        invalid_reason = "field total_count must be a uint32 value";
        goto invalid_data;
    }
    uint32_t shard_count = (uint32_t)yyjson_get_uint(shard_count_val);
    if ((size_t)shard_count != yyjson_arr_size(shard_arr)) {
        invalid_reason = "field shard_count does not match shard array length";
        goto invalid_data;
    }
    uint32_t total_count = (uint32_t)yyjson_get_uint(total_count_val);
    uint32_t expected_shard_count = storageModuleShardCount(module_type, total_count);
    if (shard_count != expected_shard_count) {
        invalid_reason = "shard count does not match total count";
        goto invalid_data;
    }

    StorageSubModuleMeta load_sm = {0};
    strncpy(load_sm.type_name, type_name, sizeof(load_sm.type_name) - 1);
    load_sm.crc32c = meta_crc32c;
    load_sm.size = meta_size;
    load_sm.total_count = total_count;
    load_sm.shard_count = shard_count;
    result = storageSubModuleMetaAdd(load_sm);
    if (result != CODE_OK) {
        return result;
    }

    for (uint32_t shard_id = 0; shard_id < shard_count; shard_id++) {
        yyjson_val* shard_item = yyjson_arr_get(shard_arr, shard_id);
        if (!yyjson_is_obj(shard_item)) {
            invalid_reason = "shard entry must be an object";
            goto invalid_data;
        }
        yyjson_val* shard_crc_val = yyjson_obj_get(shard_item, "crc32c");
        yyjson_val* shard_size_val = yyjson_obj_get(shard_item, "size");
        if (!yyjson_is_uint(shard_crc_val) || yyjson_get_uint(shard_crc_val) > UINT32_MAX || !yyjson_is_uint(shard_size_val) ||
            yyjson_get_uint(shard_size_val) > UINT32_MAX) {
            invalid_reason = "shard crc32c and size must be uint32 values";
            goto invalid_data;
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
            return result;
        }
    }

    if (module_type->read_meta_extra_fn) {
        result = module_type->read_meta_extra_fn(root, type_name, total_count);
        if (result != CODE_OK) {
            return result;
        }
    }

    yyjson_doc_free(doc);
    return CODE_OK;

invalid_data:
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid module metadata (path=%s, reason=%s)", module_meta_path,
                      invalid_reason);
    return ERR_INVALID_DATA;
}

DOMINO_CODE loadAllSubModule(yyjson_val* sub_module) {
    const char* save_path = g_domino_engine_launch_config.storage_path;

    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        DOMINO_CODE result = loadSubModuleMeta(sub_module, module_type);
        if (result != CODE_OK) {
            return result;
        }
    }

    /* 分片读取依赖已准备好的全部模块摘要。 */
    DOMINO_CODE result = CODE_OK;
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        const StorageSubModuleMeta* meta = storageSubModuleMetaFind(module_type->type_name);
        if (!meta) {
            continue;
        }
        /* 空集合也调用加载入口，避免绕过 human_ai 与人物数量等跨模块约束。 */
        result = module_type->load_fn(save_path, meta->total_count, meta->shard_count);
        if (result != CODE_OK) {
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
    int written = snprintf(path, sizeof(path), "%s/meta.json", save_path);
    if (written < 0) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to format root metadata path");
        *result = ERR_STRING_FORMAT;
        return nullptr;
    }
    if ((size_t)written >= sizeof(path)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "root metadata path exceeds buffer (capacity=%zu)", sizeof(path));
        *result = ERR_BUFFER_TOO_SMALL;
        return nullptr;
    }

    struct stat meta_stat;
    if (lstat(path, &meta_stat) != 0) {
        int stat_errno = errno;
        if (stat_errno == ENOENT) {
            *result = ERR_FILE_NOT_FOUND;
            return nullptr;
        }
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to query root metadata file (path=%s, errno=%d)", path,
                          stat_errno);
        *result = ERR_FILE_STAT;
        return nullptr;
    }

    yyjson_doc* doc = storageReadJsonFile(path, false, 0, 0, result);
    if (*result != CODE_OK) {
        return nullptr;
    }

    const char* invalid_reason = nullptr;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        invalid_reason = "root must be an object";
        goto invalid_data;
    }
    yyjson_val* version_val = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version_val)) {
        invalid_reason = "field version must be an unsigned integer";
        goto invalid_data;
    }
    uint64_t version = yyjson_get_uint(version_val);
    if (version != DOMINO_STORAGE_SAVE_VERSION) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                          "unsupported save version (path=%s, version=%" PRIu64 ", supported=%u)", path, version,
                          (unsigned)DOMINO_STORAGE_SAVE_VERSION);
        *result = ERR_UNSUPPORTED_VERSION;
        return nullptr;
    }
    yyjson_val* info = yyjson_obj_get(root, "info");
    if (!yyjson_is_obj(info)) {
        invalid_reason = "field info must be an object";
        goto invalid_data;
    }
    yyjson_val* runtime_option = yyjson_obj_get(root, "runtime_option");
    if (!yyjson_is_obj(runtime_option)) {
        invalid_reason = "field runtime_option must be an object";
        goto invalid_data;
    }
    yyjson_val* runtime_data = yyjson_obj_get(root, "runtime_data");
    if (!yyjson_is_obj(runtime_data)) {
        invalid_reason = "field runtime_data must be an object";
        goto invalid_data;
    }
    yyjson_val* game_date_time_ns_val = yyjson_obj_get(runtime_data, "game_date_time_ns");
    if (!yyjson_is_uint(game_date_time_ns_val)) {
        invalid_reason = "field runtime_data.game_date_time_ns must be an unsigned integer";
        goto invalid_data;
    }
    yyjson_val* inc = yyjson_obj_get(runtime_data, "global_increment_id");
    if (!yyjson_is_obj(inc)) {
        invalid_reason = "field runtime_data.global_increment_id must be an object";
        goto invalid_data;
    }

    yyjson_val* sub_module = yyjson_obj_get(root, "sub_module");
    if (!yyjson_is_obj(sub_module)) {
        invalid_reason = "field sub_module must be an object";
        goto invalid_data;
    }

    yyjson_val* integrity_verify_val = yyjson_obj_get(root, "integrity_verify");
    if (!yyjson_is_bool(integrity_verify_val)) {
        invalid_reason = "field integrity_verify must be a boolean";
        goto invalid_data;
    }
    storageSubModuleSetIntegrityVerify(yyjson_get_bool(integrity_verify_val));

    serdeReadStr(info, "account_id", g_domino_storage_account_id, sizeof(g_domino_storage_account_id));
    serdeReadStr(runtime_option, "run_mode", g_domino_storage_run_mode, sizeof(g_domino_storage_run_mode));
    dominoSetRuntimeDateTimeBase(yyjson_get_uint(game_date_time_ns_val));
    *result = ingestIncrementIds(inc);
    if (*result != CODE_OK) {
        return nullptr;
    }
    return doc;
invalid_data:
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid root metadata (path=%s, reason=%s)", path, invalid_reason);
    *result = ERR_INVALID_DATA;
    return nullptr;
}
