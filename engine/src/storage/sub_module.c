#include "sub_module.h"

#include <stdlib.h>
#include <string.h>

#include "../logger/entry.h"

static StorageSubModuleMeta g_storage_sub_modules[STORAGE_SUB_MODULE_MAX_COUNT];
static size_t g_storage_sub_modules_len = 0;

static StorageSubModuleShard* g_storage_sub_module_shards = nullptr;
static size_t g_storage_sub_module_shards_len = 0;
static size_t g_storage_sub_module_shards_cap = 0;
static bool g_storage_sub_module_integrity_verify = false;

static DOMINO_CODE storageCopyFixedString(char* out, size_t out_size, const char* src_str, const char* field_name) {
    if (!out || out_size == 0 || !src_str) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid sub_module string field: %s",
                          field_name ? field_name : "(unknown)");
        return ERR_INVALID_PARAM;
    }

    size_t len = 0;
    while (len < out_size && src_str[len] != '\0') {
        len++;
    }
    if (len >= out_size) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "sub_module string too long: %s",
                          field_name ? field_name : "(unknown)");
        return ERR_OUT_OF_RANGE;
    }

    memcpy(out, src_str, len);
    out[len] = '\0';
    return CODE_OK;
}

DOMINO_CODE storageSubModuleReset(void) {
    memset(g_storage_sub_modules, 0, sizeof(g_storage_sub_modules));
    g_storage_sub_modules_len = 0;
    g_storage_sub_module_integrity_verify = false;

    if (!g_storage_sub_module_shards) {
        g_storage_sub_module_shards =
            (StorageSubModuleShard*)malloc((size_t)STORAGE_SUB_MODULE_SHARD_RESERVE_MIN_COUNT * sizeof(StorageSubModuleShard));
        if (!g_storage_sub_module_shards) {
            return ERR_MEMORY_ALLOC;
        }
        g_storage_sub_module_shards_cap = (size_t)STORAGE_SUB_MODULE_SHARD_RESERVE_MIN_COUNT;
    } else {
        memset(g_storage_sub_module_shards, 0, g_storage_sub_module_shards_cap * sizeof(StorageSubModuleShard));
    }
    g_storage_sub_module_shards_len = 0;
    return CODE_OK;
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

DOMINO_CODE storageSubModuleMetaAdd(StorageSubModuleMeta meta) {
    char type_name[STORAGE_SUB_MODULE_TYPE_MAX_LENGTH];
    DOMINO_CODE result = storageCopyFixedString(type_name, sizeof(type_name), meta.type_name, "type_name");
    if (result != CODE_OK) {
        return result;
    }

    if (storageSubModuleMetaFind(type_name) != nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "sub_module meta duplicate: %s", type_name);
        return ERR_ALREADY_EXISTS;
    }

    if (g_storage_sub_modules_len >= (size_t)STORAGE_SUB_MODULE_MAX_COUNT) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "sub_module overflow type=%s", type_name);
        return ERR_OUT_OF_RANGE;
    }
    StorageSubModuleMeta* new_item = &g_storage_sub_modules[g_storage_sub_modules_len++];
    memset(new_item, 0, sizeof(*new_item));
    memcpy(new_item->type_name, type_name, strlen(type_name) + 1);
    new_item->crc32c = meta.crc32c;
    new_item->size = meta.size;
    new_item->total_count = meta.total_count;
    new_item->shard_count = meta.shard_count;
    return CODE_OK;
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

DOMINO_CODE storageSubModuleShardAdd(StorageSubModuleShard shard) {
    char type_name[STORAGE_SUB_MODULE_TYPE_MAX_LENGTH];
    DOMINO_CODE result = storageCopyFixedString(type_name, sizeof(type_name), shard.type_name, "type_name");
    if (result != CODE_OK) {
        return result;
    }

    if (storageSubModuleShardFind(type_name, shard.shard_id) != nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid meta.json: duplicate shard digest for %s shard=%u",
                          type_name, shard.shard_id);
        return ERR_ALREADY_EXISTS;
    }
    if (g_storage_sub_module_shards_len == g_storage_sub_module_shards_cap) {
        size_t new_cap =
            g_storage_sub_module_shards_cap == 0 ? (size_t)STORAGE_SUB_MODULE_SHARD_RESERVE_MIN_COUNT : g_storage_sub_module_shards_cap * 2;
        StorageSubModuleShard* new_ptr = (StorageSubModuleShard*)realloc(g_storage_sub_module_shards, new_cap * sizeof(StorageSubModuleShard));
        if (!new_ptr) {
            return ERR_MEMORY_ALLOC;
        }
        g_storage_sub_module_shards = new_ptr;
        g_storage_sub_module_shards_cap = new_cap;
    }
    StorageSubModuleShard* out = &g_storage_sub_module_shards[g_storage_sub_module_shards_len++];
    memcpy(out->type_name, type_name, strlen(type_name) + 1);
    out->shard_id = shard.shard_id;
    out->crc32c = shard.crc32c;
    out->size = shard.size;
    return CODE_OK;
}
