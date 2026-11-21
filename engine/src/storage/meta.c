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
#include "manifest.h"
#include "serde.h"
#include "serde_registry.h"

/** @brief 写一个模块的 meta 文件（实体数、分片数、分片摘要），输出该文件自身的摘要供根 meta 记录。 */
static void writeModuleMetaFile(const char* save_path, const StorageModuleDescriptor* module_ptr, uint32_t* crc32c_out, uint32_t* size_out) {
    const char* type_name = module_ptr->type_name;
    uint32_t total_count = (uint32_t)module_ptr->count_fn();
    uint32_t shard_count = storageModuleShardCount(module_ptr, total_count);
    char path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s.meta.json", save_path, type_name);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format module metadata path (module=%s)", type_name);
    }
    if ((size_t)written >= sizeof(path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "module metadata path exceeds buffer (capacity=%zu)", sizeof(path));
    }
    bool integrity_check = storageManifestChecksIntegrity();

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

    if (module_ptr->write_meta_extra_fn) {
        module_ptr->write_meta_extra_fn(doc, root, type_name);
    }

    yyjson_mut_val* shard_digest_arr = yyjson_mut_obj_add_arr(doc, root, "shards");
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
        if (integrity_check) {
            const StorageShardDigest* digest_ptr = storageManifestFindShard(type_name, shard_id);
            shard_crc32c = digest_ptr->crc32c;
            shard_size = digest_ptr->size;
        }
        if (!yyjson_mut_obj_add_uint(doc, item, "crc32c", shard_crc32c) || !yyjson_mut_obj_add_uint(doc, item, "size", shard_size)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate module metadata JSON (path=%s)", path);
        }
    }

    storageWriteJsonFile(path, doc, integrity_check, crc32c_out, size_out);
    yyjson_mut_doc_free(doc);
}

/* 根 meta 的 id_counters：JSON key 与 DominoIdCounters 字段同名。road ID 由两端路口 ID 组成，没有独立计数器。 */
#define ID_COUNTER_FIELD(field) {#field, &g_domino_id_counters.field}
static const struct {
    const char* key;
    _Atomic uint32_t* counter_ptr;
} g_id_counter_fields[] = {
    ID_COUNTER_FIELD(next_human_id),          ID_COUNTER_FIELD(last_motivation_id), ID_COUNTER_FIELD(last_task_id),
    ID_COUNTER_FIELD(last_behavior_id),       ID_COUNTER_FIELD(next_org_id),        ID_COUNTER_FIELD(next_country_id),
    ID_COUNTER_FIELD(next_city_id),           ID_COUNTER_FIELD(next_island_id),     ID_COUNTER_FIELD(next_building_id),
    ID_COUNTER_FIELD(next_fork_road_id),      ID_COUNTER_FIELD(next_road_line_id),  ID_COUNTER_FIELD(next_asset_id),
    ID_COUNTER_FIELD(next_movable_object_id), ID_COUNTER_FIELD(next_name_id),       ID_COUNTER_FIELD(next_description_id),
};
#undef ID_COUNTER_FIELD

static void writeIdCounters(yyjson_mut_doc* doc, yyjson_mut_val* id_counter_obj) {
    for (size_t i = 0; i < sizeof(g_id_counter_fields) / sizeof(g_id_counter_fields[0]); i++) {
        if (!yyjson_mut_obj_add_uint(doc, id_counter_obj, g_id_counter_fields[i].key, atomic_load(g_id_counter_fields[i].counter_ptr))) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate ID counter metadata (key=%s)", g_id_counter_fields[i].key);
        }
    }
}

static void readIdCounters(yyjson_val* id_counter_obj) {
    for (size_t i = 0; i < sizeof(g_id_counter_fields) / sizeof(g_id_counter_fields[0]); i++) {
        atomic_store(g_id_counter_fields[i].counter_ptr, (uint32_t)yyjson_get_uint(yyjson_obj_get(id_counter_obj, g_id_counter_fields[i].key)));
    }
}

void storageSaveAllShards(const char* save_path) {
    if (dominoMkdir(save_path) != 0) {
        int mkdir_errno = errno;
        DOMINO_EXIT_WITH_ERROR(ERR_DIRECTORY_CREATE, "failed to create save directory (path=%s, errno=%d)", save_path, mkdir_errno);
    }
    for (size_t i = 0; i < g_storage_module_registry_count; i++) {
        g_storage_module_registry[i].save_fn(save_path);
    }
}

void storageSaveMeta(const char* save_path) {
    char path[DOMINO_STORAGE_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/meta.json", save_path);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format root metadata path");
    }
    if ((size_t)written >= sizeof(path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "root metadata path exceeds buffer (capacity=%zu)", sizeof(path));
    }

    bool integrity_check = storageManifestChecksIntegrity();
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
        !yyjson_mut_obj_add_bool(doc, root, "integrity_check", integrity_check)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }

    if (!yyjson_mut_obj_add_str(doc, root, "account_id", g_domino_account_id) || !yyjson_mut_obj_add_str(doc, root, "run_mode", g_domino_run_mode) ||
        !yyjson_mut_obj_add_uint(doc, root, "runtime_base_ns", dominoTimeGetRuntimeBaseNs())) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }
    yyjson_mut_val* id_counter_obj = yyjson_mut_obj_add_obj(doc, root, "id_counters");
    if (!id_counter_obj) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }
    writeIdCounters(doc, id_counter_obj);

    yyjson_mut_val* module_digest_obj = yyjson_mut_obj_add_obj(doc, root, "modules");
    if (!module_digest_obj) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
    }
    for (size_t i = 0; i < g_storage_module_registry_count; i++) {
        const StorageModuleDescriptor* module_ptr = &g_storage_module_registry[i];
        uint32_t crc32c = 0;
        uint32_t size = 0;
        writeModuleMetaFile(save_path, module_ptr, &crc32c, &size);
        yyjson_mut_val* module_obj = yyjson_mut_obj_add_obj(doc, module_digest_obj, module_ptr->type_name);
        if (!module_obj || !yyjson_mut_obj_add_uint(doc, module_obj, "crc32c", crc32c) || !yyjson_mut_obj_add_uint(doc, module_obj, "size", size)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate root metadata JSON (path=%s)", path);
        }
    }

    storageWriteJsonFile(path, doc, false, nullptr, nullptr);
    yyjson_mut_doc_free(doc);
}

/** @brief 读取一个模块 meta，把模块和各分片摘要登记到清单；模块扩展字段（如 Nav 分段）一并恢复。 */
static void loadModuleMeta(yyjson_val* module_digest_obj, const StorageModuleDescriptor* module_ptr) {
    const char* type_name = module_ptr->type_name;
    bool integrity_check = storageManifestChecksIntegrity();
    yyjson_val* item = yyjson_obj_get(module_digest_obj, type_name);
    yyjson_val* crc_val = yyjson_obj_get(item, "crc32c");
    yyjson_val* size_val = yyjson_obj_get(item, "size");
    uint32_t meta_crc32c = 0;
    uint32_t meta_size = 0;
    if (integrity_check) {
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
    yyjson_doc* doc = storageReadJsonFile(module_meta_path, integrity_check, meta_crc32c, meta_size);
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* shard_arr = yyjson_obj_get(root, "shards");
    uint32_t shard_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(root, "shard_count"));
    uint32_t total_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(root, "total_count"));

    StorageModuleMeta module_meta = {0};
    strncpy(module_meta.type_name, type_name, sizeof(module_meta.type_name) - 1);
    module_meta.crc32c = meta_crc32c;
    module_meta.size = meta_size;
    module_meta.total_count = total_count;
    module_meta.shard_count = shard_count;
    storageManifestAddModule(module_meta);

    for (uint32_t shard_id = 0; shard_id < shard_count; shard_id++) {
        yyjson_val* shard_item = yyjson_arr_get(shard_arr, shard_id);
        yyjson_val* shard_crc_val = yyjson_obj_get(shard_item, "crc32c");
        yyjson_val* shard_size_val = yyjson_obj_get(shard_item, "size");
        uint32_t crc32c = (uint32_t)yyjson_get_uint(shard_crc_val);
        uint32_t size = (uint32_t)yyjson_get_uint(shard_size_val);
        StorageShardDigest shard_digest = {0};
        strncpy(shard_digest.type_name, type_name, sizeof(shard_digest.type_name) - 1);
        shard_digest.shard_id = shard_id;
        shard_digest.crc32c = crc32c;
        shard_digest.size = size;
        storageManifestAddShard(shard_digest);
    }

    if (module_ptr->read_meta_extra_fn) {
        module_ptr->read_meta_extra_fn(root, type_name);
    }

    yyjson_doc_free(doc);
}

void storageLoadAllModules(yyjson_val* module_digest_obj) {
    const char* save_path = g_domino_engine_launch_config.storage_path;

    for (size_t i = 0; i < g_storage_module_registry_count; i++) {
        const StorageModuleDescriptor* module_ptr = &g_storage_module_registry[i];
        loadModuleMeta(module_digest_obj, module_ptr);
    }

    // 分片读取依赖清单中已登记的全部摘要。
    for (size_t i = 0; i < g_storage_module_registry_count; i++) {
        const StorageModuleDescriptor* module_ptr = &g_storage_module_registry[i];
        const StorageModuleMeta* meta = storageManifestFindModule(module_ptr->type_name);
        module_ptr->load_fn(save_path, meta->total_count, meta->shard_count);
    }

    // 全部实体就位后才绑定指针，避免容器扩容让已绑定的地址失效。
    for (size_t i = 0; i < g_storage_module_registry_count; i++) {
        const StorageModuleDescriptor* module_ptr = &g_storage_module_registry[i];
        if (module_ptr->bind_references_fn) {
            module_ptr->bind_references_fn();
        }
    }
}

yyjson_doc* storageLoadMeta(void) {
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
    storageManifestSetIntegrityCheck(yyjson_get_bool(yyjson_obj_get(root, "integrity_check")));
    serdeReadStr(root, "account_id", g_domino_account_id, sizeof(g_domino_account_id));
    serdeReadStr(root, "run_mode", g_domino_run_mode, sizeof(g_domino_run_mode));
    dominoTimeSetRuntimeBaseNs(yyjson_get_uint(yyjson_obj_get(root, "runtime_base_ns")));
    readIdCounters(yyjson_obj_get(root, "id_counters"));
    return doc;
}
