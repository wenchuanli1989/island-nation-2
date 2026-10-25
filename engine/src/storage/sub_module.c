#include "sub_module.h"

#include <stdlib.h>
#include <string.h>

#include "domino_shared_common.h"

static StorageSubModuleMeta g_storage_sub_modules[STORAGE_SUB_MODULE_MAX_COUNT];
static size_t g_storage_sub_modules_len = 0;

static StorageSubModuleShard* g_storage_sub_module_shards = nullptr;
static size_t g_storage_sub_module_shards_len = 0;
static size_t g_storage_sub_module_shards_cap = 0;
static bool g_storage_sub_module_integrity_verify = false;

void storageSubModuleInit(void) {
    g_storage_sub_module_shards = (StorageSubModuleShard*)malloc((size_t)STORAGE_SUB_MODULE_SHARD_RESERVE_MIN_COUNT * sizeof(StorageSubModuleShard));
    if (!g_storage_sub_module_shards) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate shard metadata (count=%u)", STORAGE_SUB_MODULE_SHARD_RESERVE_MIN_COUNT);
    }
    g_storage_sub_module_shards_cap = (size_t)STORAGE_SUB_MODULE_SHARD_RESERVE_MIN_COUNT;
}

void storageSubModuleSetIntegrityVerify(bool integrity_verify) {
    g_storage_sub_module_integrity_verify = integrity_verify;
}

bool storageSubModuleIntegrityVerify(void) {
    return g_storage_sub_module_integrity_verify;
}

void storageSubModuleDestroy(void) {
    memset(g_storage_sub_modules, 0, sizeof(g_storage_sub_modules));
    g_storage_sub_modules_len = 0;

    free(g_storage_sub_module_shards);
    g_storage_sub_module_shards = nullptr;
    g_storage_sub_module_shards_len = 0;
    g_storage_sub_module_shards_cap = 0;
    g_storage_sub_module_integrity_verify = false;
}

void storageSubModuleMetaAdd(StorageSubModuleMeta meta) {
    if (g_storage_sub_modules_len >= (size_t)STORAGE_SUB_MODULE_MAX_COUNT) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "module metadata capacity exhausted (type=%s, count=%zu)", meta.type_name,
                               g_storage_sub_modules_len);
    }
    g_storage_sub_modules[g_storage_sub_modules_len++] = meta;
}

const StorageSubModuleMeta* storageSubModuleMetaFind(const char* type_name) {
    for (size_t i = 0; i < g_storage_sub_modules_len; i++) {
        StorageSubModuleMeta* item = &g_storage_sub_modules[i];
        if (strncmp(item->type_name, type_name, sizeof(item->type_name)) == 0) {
            return item;
        }
    }
    return nullptr;
}

const StorageSubModuleShard* storageSubModuleShardFind(const char* type_name, uint32_t shard_id) {
    for (size_t i = 0; i < g_storage_sub_module_shards_len; i++) {
        StorageSubModuleShard* item = &g_storage_sub_module_shards[i];
        if (item->shard_id == shard_id && strncmp(item->type_name, type_name, sizeof(item->type_name)) == 0) {
            return item;
        }
    }
    return nullptr;
}

void storageSubModuleShardAdd(StorageSubModuleShard shard) {
    if (g_storage_sub_module_shards_len == g_storage_sub_module_shards_cap) {
        if (g_storage_sub_module_shards_cap > SIZE_MAX / 2U / sizeof(StorageSubModuleShard)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "shard metadata allocation size overflows (capacity=%zu)", g_storage_sub_module_shards_cap);
        }
        size_t new_cap = g_storage_sub_module_shards_cap * 2;
        StorageSubModuleShard* new_ptr = (StorageSubModuleShard*)realloc(g_storage_sub_module_shards, new_cap * sizeof(StorageSubModuleShard));
        if (!new_ptr) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to grow shard metadata (capacity=%zu)", new_cap);
        }
        g_storage_sub_module_shards = new_ptr;
        g_storage_sub_module_shards_cap = new_cap;
    }
    g_storage_sub_module_shards[g_storage_sub_module_shards_len++] = shard;
}
