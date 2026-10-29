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
#include "../time/entry.h"
#include "domino_shared_common.h"
#include "entry.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"
#include "sub_module.h"

/** @brief 分片已全部写入后，写模块 meta 并直接返回根 meta 所需摘要。 */
static void writeOneModuleMetaFile(const char* save_path, const StorageModuleType* module_type, uint32_t* crc32c_out, uint32_t* size_out) {
    const char* type_name = module_type->type_name;
    uint32_t total_count = (uint32_t)module_type->count_fn();
    uint32_t shard_count = storageModuleShardCount(module_type, total_count);
    char path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s.meta.json", save_path, type_name);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format module metadata path (module=%s)", type_name);
    }
    if ((size_t)written >= sizeof(path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "module metadata path exceeds buffer (capacity=%zu)", sizeof(path));
    }
    bool do_verify = storageSubModuleIntegrityVerify();

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate module metadata JSON (path=%s)", path);
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    if (!root) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate module metadata JSON (path=%s)", path);
    }
    yyjson_mut_doc_set_root(doc, root);

    if (!yyjson_mut_obj_add_str(doc, root, "type", type_name) || !yyjson_mut_obj_add_uint(doc, root, "total_count", total_count) ||
        !yyjson_mut_obj_add_uint(doc, root, "shard_count", shard_count)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate module metadata JSON (path=%s)", path);
    }

    if (module_type->write_meta_extra_fn) {
        module_type->write_meta_extra_fn(doc, root, type_name);
    }

    yyjson_mut_val* shard_digest_arr = yyjson_mut_obj_add_arr(doc, root, "shard");
    if (!shard_digest_arr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate module metadata JSON (path=%s)", path);
    }
    for (uint32_t shard_id = 0; shard_id < shard_count; shard_id++) {
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, shard_digest_arr);
        if (!item) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate module metadata JSON (path=%s)", path);
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
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate module metadata JSON (path=%s)", path);
        }
    }

    storageWriteJsonFile(path, doc, do_verify, crc32c_out, size_out);
    yyjson_mut_doc_free(doc);
}

static void emitIncrementIds(yyjson_mut_doc* doc, yyjson_mut_val* inc) {
    /* ID 计数器独立持久化；road ID 由两端节点 ID 组合，无独立计数器。 */
    if (!yyjson_mut_obj_add_uint(doc, inc, "human", atomic_load(&g_domino_global_increment_id.human_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "task", atomic_load(&g_domino_global_increment_id.task_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "org", atomic_load(&g_domino_global_increment_id.org_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "country", atomic_load(&g_domino_global_increment_id.country_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "city", atomic_load(&g_domino_global_increment_id.city_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "island", atomic_load(&g_domino_global_increment_id.island_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "building", atomic_load(&g_domino_global_increment_id.building_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "fork_road", atomic_load(&g_domino_global_increment_id.fork_road_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "road_line", atomic_load(&g_domino_global_increment_id.road_line_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "asset", atomic_load(&g_domino_global_increment_id.asset_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "movable_object", atomic_load(&g_domino_global_increment_id.movable_object_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "name", atomic_load(&g_domino_global_increment_id.name_increment_id)) ||
        !yyjson_mut_obj_add_uint(doc, inc, "description", atomic_load(&g_domino_global_increment_id.description_increment_id))) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate global increment ID metadata");
    }
}

static void ingestIncrementIds(yyjson_val* inc) {
    yyjson_val* task_val = yyjson_obj_get(inc, "task");
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
}

void saveAllShards(const char* save_path) {
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        if (module_type->validate_save_fn) {
            module_type->validate_save_fn();
        }
    }
    if (dominoMkdir(save_path) != 0) {
        int mkdir_errno = errno;
        DOMINO_EXIT_WITH_ERROR(ERR_DIRECTORY_CREATE, "failed to create save directory (path=%s, errno=%d)", save_path, mkdir_errno);
    }
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        g_storage_module_types[i].save_fn(save_path);
    }
}

void metaSave(const char* save_path) {
    char path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/meta.json", save_path);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format root metadata path");
    }
    if ((size_t)written >= sizeof(path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "root metadata path exceeds buffer (capacity=%zu)", sizeof(path));
    }

    bool do_verify = storageSubModuleIntegrityVerify();
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    if (!root) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }
    yyjson_mut_doc_set_root(doc, root);

    if (!yyjson_mut_obj_add_uint(doc, root, "version", DOMINO_STORAGE_SAVE_VERSION) ||
        !yyjson_mut_obj_add_bool(doc, root, "integrity_verify", do_verify)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }

    yyjson_mut_val* info = yyjson_mut_obj_add_obj(doc, root, "info");
    if (!info || !yyjson_mut_obj_add_str(doc, info, "account_id", g_domino_storage_account_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }

    yyjson_mut_val* runtime_option = yyjson_mut_obj_add_obj(doc, root, "runtime_option");
    if (!runtime_option || !yyjson_mut_obj_add_str(doc, runtime_option, "run_mode", g_domino_storage_run_mode)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }

    yyjson_mut_val* runtime_data = yyjson_mut_obj_add_obj(doc, root, "runtime_data");
    if (!runtime_data || !yyjson_mut_obj_add_uint(doc, runtime_data, "game_date_time_ns", dominoGetRuntimeDateTimeBase())) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }
    yyjson_mut_val* inc = yyjson_mut_obj_add_obj(doc, runtime_data, "global_increment_id");
    if (!inc) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }
    emitIncrementIds(doc, inc);

    yyjson_mut_val* sub_module = yyjson_mut_obj_add_obj(doc, root, "sub_module");
    if (!sub_module) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        uint32_t crc32c = 0;
        uint32_t size = 0;
        writeOneModuleMetaFile(save_path, module_type, &crc32c, &size);
        yyjson_mut_val* module_obj = yyjson_mut_obj_add_obj(doc, sub_module, module_type->type_name);
        if (!module_obj || !yyjson_mut_obj_add_uint(doc, module_obj, "crc32c", crc32c) || !yyjson_mut_obj_add_uint(doc, module_obj, "size", size)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
        }
    }

    storageWriteJsonFile(path, doc, false, nullptr, nullptr);
    yyjson_mut_doc_free(doc);
}

/** @brief 先读取模块 meta 建立分片摘要索引，再加载实体分片。 */
static void loadSubModuleMeta(yyjson_val* sub_module, const StorageModuleType* module_type) {
    const char* type_name = module_type->type_name;
    bool do_verify = storageSubModuleIntegrityVerify();
    yyjson_val* item = yyjson_obj_get(sub_module, type_name);
    yyjson_val* crc_val = yyjson_obj_get(item, "crc32c");
    yyjson_val* size_val = yyjson_obj_get(item, "size");
    uint32_t meta_crc32c = 0;
    uint32_t meta_size = 0;
    if (do_verify) {
        meta_crc32c = (uint32_t)yyjson_get_uint(crc_val);
        meta_size = (uint32_t)yyjson_get_uint(size_val);
    }
    char module_meta_path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(module_meta_path, sizeof(module_meta_path), "%s/%s.meta.json", g_domino_engine_launch_config.storage_path, type_name);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format module metadata path (module=%s)", type_name);
    }
    if ((size_t)written >= sizeof(module_meta_path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "module metadata path exceeds buffer (module=%s, capacity=%zu)", type_name,
                               sizeof(module_meta_path));
    }
    yyjson_doc* doc = storageReadJsonFile(module_meta_path, do_verify, meta_crc32c, meta_size);
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* shard_arr = yyjson_obj_get(root, "shard");
    uint32_t shard_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(root, "shard_count"));
    uint32_t total_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(root, "total_count"));

    StorageSubModuleMeta load_sm = {0};
    strncpy(load_sm.type_name, type_name, sizeof(load_sm.type_name) - 1);
    load_sm.crc32c = meta_crc32c;
    load_sm.size = meta_size;
    load_sm.total_count = total_count;
    load_sm.shard_count = shard_count;
    storageSubModuleMetaAdd(load_sm);

    for (uint32_t shard_id = 0; shard_id < shard_count; shard_id++) {
        yyjson_val* shard_item = yyjson_arr_get(shard_arr, shard_id);
        yyjson_val* shard_crc_val = yyjson_obj_get(shard_item, "crc32c");
        yyjson_val* shard_size_val = yyjson_obj_get(shard_item, "size");
        uint32_t crc32c = (uint32_t)yyjson_get_uint(shard_crc_val);
        uint32_t size = (uint32_t)yyjson_get_uint(shard_size_val);
        StorageSubModuleShard shard_row = {0};
        strncpy(shard_row.type_name, type_name, sizeof(shard_row.type_name) - 1);
        shard_row.shard_id = shard_id;
        shard_row.crc32c = crc32c;
        shard_row.size = size;
        storageSubModuleShardAdd(shard_row);
    }

    if (module_type->read_meta_extra_fn) {
        module_type->read_meta_extra_fn(root, type_name);
    }

    yyjson_doc_free(doc);
}

void loadAllSubModule(yyjson_val* sub_module) {
    const char* save_path = g_domino_engine_launch_config.storage_path;

    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        loadSubModuleMeta(sub_module, module_type);
    }

    /* 分片读取依赖已准备好的全部模块摘要。 */
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        const StorageSubModuleMeta* meta = storageSubModuleMetaFind(module_type->type_name);
        /* 当前格式为每个注册项写出 meta，空集合也保持同一加载路径。 */
        module_type->load_fn(save_path, meta->total_count, meta->shard_count);
    }

    /* 主表和 ID map 已稳定；只构造当前进程的指针和调度状态。 */
    for (size_t i = 0; i < g_storage_module_type_count; i++) {
        const StorageModuleType* module_type = &g_storage_module_types[i];
        if (module_type->bind_references_fn) {
            module_type->bind_references_fn();
        }
    }
}

yyjson_doc* metaLoad(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;
    char path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/meta.json", save_path);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format root metadata path");
    }
    if ((size_t)written >= sizeof(path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "root metadata path exceeds buffer (capacity=%zu)", sizeof(path));
    }

    struct stat meta_stat;
    if (lstat(path, &meta_stat) != 0) {
        int stat_errno = errno;
        if (stat_errno == ENOENT) {
            return nullptr;
        }
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_STAT, "failed to query root metadata file (path=%s, errno=%d)", path, stat_errno);
    }

    yyjson_doc* doc = storageReadJsonFile(path, false, 0, 0);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* version_val = yyjson_obj_get(root, "version");
    uint64_t version = yyjson_get_uint(version_val);
    if (version != DOMINO_STORAGE_SAVE_VERSION) {
        DOMINO_EXIT_WITH_ERROR(ERR_UNSUPPORTED_VERSION, "unsupported save version (path=%s, version=%" PRIu64 ", supported=%u)", path, version,
                               (unsigned)DOMINO_STORAGE_SAVE_VERSION);
    }
    yyjson_val* info = yyjson_obj_get(root, "info");
    yyjson_val* runtime_option = yyjson_obj_get(root, "runtime_option");
    yyjson_val* runtime_data = yyjson_obj_get(root, "runtime_data");
    yyjson_val* game_date_time_ns_val = yyjson_obj_get(runtime_data, "game_date_time_ns");
    yyjson_val* inc = yyjson_obj_get(runtime_data, "global_increment_id");

    yyjson_val* integrity_verify_val = yyjson_obj_get(root, "integrity_verify");
    storageSubModuleSetIntegrityVerify(yyjson_get_bool(integrity_verify_val));

    serdeReadStr(info, "account_id", g_domino_storage_account_id, sizeof(g_domino_storage_account_id));
    serdeReadStr(runtime_option, "run_mode", g_domino_storage_run_mode, sizeof(g_domino_storage_run_mode));
    dominoSetRuntimeDateTimeBase(yyjson_get_uint(game_date_time_ns_val));
    ingestIncrementIds(inc);
    return doc;
}
