#include "manifest.h"

#include <stdlib.h>
#include <string.h>

#include "domino_shared_common.h"

static StorageModuleMeta g_module_meta_list[STORAGE_MODULE_COUNT_MAX];
static size_t g_module_meta_count = 0;

static StorageShardDigest* g_shard_digest_list = nullptr;
static size_t g_shard_digest_count = 0;
static size_t g_shard_digest_capacity = 0;
static bool g_integrity_check_flag = false;

void storageManifestInit(void) {
    g_shard_digest_list = (StorageShardDigest*)malloc((size_t)STORAGE_SHARD_DIGEST_INITIAL_CAPACITY * sizeof(StorageShardDigest));
    if (!g_shard_digest_list) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate shard metadata (count=%u)", STORAGE_SHARD_DIGEST_INITIAL_CAPACITY);
    }
    g_shard_digest_capacity = (size_t)STORAGE_SHARD_DIGEST_INITIAL_CAPACITY;
}

void storageManifestSetIntegrityCheck(bool integrity_check) {
    g_integrity_check_flag = integrity_check;
}

bool storageManifestChecksIntegrity(void) {
    return g_integrity_check_flag;
}

void storageManifestDestroy(void) {
    memset(g_module_meta_list, 0, sizeof(g_module_meta_list));
    g_module_meta_count = 0;

    free(g_shard_digest_list);
    g_shard_digest_list = nullptr;
    g_shard_digest_count = 0;
    g_shard_digest_capacity = 0;
    g_integrity_check_flag = false;
}

void storageManifestAddModule(StorageModuleMeta module_meta) {
    if (g_module_meta_count >= (size_t)STORAGE_MODULE_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "module metadata capacity exhausted (type=%s, count=%zu)", module_meta.type_name,
                               g_module_meta_count);
    }
    g_module_meta_list[g_module_meta_count++] = module_meta;
}

const StorageModuleMeta* storageManifestFindModule(const char* type_name) {
    for (size_t i = 0; i < g_module_meta_count; i++) {
        const StorageModuleMeta* meta_ptr = &g_module_meta_list[i];
        if (strncmp(meta_ptr->type_name, type_name, sizeof(meta_ptr->type_name)) == 0) {
            return meta_ptr;
        }
    }
    return nullptr;
}

const StorageShardDigest* storageManifestFindShard(const char* type_name, uint32_t shard_id) {
    for (size_t i = 0; i < g_shard_digest_count; i++) {
        const StorageShardDigest* digest_ptr = &g_shard_digest_list[i];
        if (digest_ptr->shard_id == shard_id && strncmp(digest_ptr->type_name, type_name, sizeof(digest_ptr->type_name)) == 0) {
            return digest_ptr;
        }
    }
    return nullptr;
}

void storageManifestAddShard(StorageShardDigest shard_digest) {
    if (g_shard_digest_count == g_shard_digest_capacity) {
        if (g_shard_digest_capacity > SIZE_MAX / 2U / sizeof(StorageShardDigest)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "shard metadata allocation size overflows (capacity=%zu)", g_shard_digest_capacity);
        }
        size_t new_capacity = g_shard_digest_capacity * 2;
        StorageShardDigest* new_list = (StorageShardDigest*)realloc(g_shard_digest_list, new_capacity * sizeof(StorageShardDigest));
        if (!new_list) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to grow shard metadata (capacity=%zu)", new_capacity);
        }
        g_shard_digest_list = new_list;
        g_shard_digest_capacity = new_capacity;
    }
    g_shard_digest_list[g_shard_digest_count++] = shard_digest;
}
