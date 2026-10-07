#ifndef DOMINO_ENGINE_STORAGE_SERDE_REGISTRY_H
#define DOMINO_ENGINE_STORAGE_SERDE_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../logger/entry.h"
#include "domino_shared_error_codes.h"
#include "io.h"
#include "klib/khashl.h"

#define DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA 8000
#define DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA 100000
#define STORAGE_NO_EXTRA_LOAD_RESERVE ((void)0);
#define STORAGE_KHASHL_MAX_BUCKETS ((size_t)1U << 31)
#define STORAGE_KHASHL_MAX_LOAD_COUNT (STORAGE_KHASHL_MAX_BUCKETS - (STORAGE_KHASHL_MAX_BUCKETS >> 2))

/** @brief 依据 khashl 默认 75% 负载因子，计算可容纳 value_count 个元素的桶数。 */
static inline bool storageComputeHashBucketReserve(size_t value_count, khint_t* out_bucket_count) {
    if (!out_bucket_count) {
        return false;
    }
    if (value_count == 0) {
        *out_bucket_count = 0;
        return true;
    }
    if (value_count > STORAGE_KHASHL_MAX_LOAD_COUNT) {
        return false;
    }
    size_t bucket_count = value_count + ((value_count + 2U) / 3U);
    if (bucket_count < 4U) {
        bucket_count = 4U;
    }
    *out_bucket_count = (khint_t)bucket_count;
    return true;
}

#define STORAGE_RESERVE_ID_MAP(type_str, id_map, id_map_resize, reserve_count)                                                                  \
    do {                                                                                                                                        \
        if (!(id_map)) {                                                                                                                        \
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "missing " type_str " id map");                         \
            return ERR_NULL_POINTER;                                                                                                            \
        }                                                                                                                                       \
        khint_t id_map_bucket_reserve = 0;                                                                                                      \
        if (!storageComputeHashBucketReserve((reserve_count), &id_map_bucket_reserve)) {                                                        \
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "too many " type_str " entries to reserve id map: %zu", \
                              (size_t)(reserve_count));                                                                                         \
            return ERR_INVALID_DATA;                                                                                                            \
        }                                                                                                                                       \
        if (id_map_bucket_reserve > kh_capacity(id_map) && id_map_resize((id_map), id_map_bucket_reserve) < 0) {                                \
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to reserve " type_str " id map buckets=%u",     \
                              (uint32_t)id_map_bucket_reserve);                                                                                 \
            return ERR_MEMORY_ALLOC;                                                                                                            \
        }                                                                                                                                       \
    } while (0)

typedef uint32_t (*StorageShardCountFn)(uint32_t total_count, uint32_t max_per_shard);
typedef DOMINO_CODE (*StorageWriteModuleMetaExtraFn)(yyjson_mut_doc* doc, yyjson_mut_val* root, const char* type_name);
typedef DOMINO_CODE (*StorageReadModuleMetaExtraFn)(yyjson_val* root, const char* type_name);

/** @brief 只读校验模块的跨模块引用；不得修改实体或其他模块状态。 */
typedef DOMINO_CODE (*StorageValidateModuleReferencesFn)(void);

/** @brief 在全部模块引用校验成功后绑定运行时引用；该操作必须不可失败。 */
typedef void (*StorageBindModuleReferencesFn)(void);

/** @brief regions 使用单文件专用格式，其余实体使用 {type_name}_{shard_id}.json。 */
typedef struct {
    const char* type_name;
    uint32_t max_per_shard;
    bool required;
    size_t (*count_fn)(void);
    StorageShardCountFn shard_count_fn;
    DOMINO_CODE (*save_fn)(const char* save_path);
    DOMINO_CODE (*load_fn)(const char* save_path, uint32_t total, uint32_t shard_count);
    StorageWriteModuleMetaExtraFn write_meta_extra_fn;
    StorageReadModuleMetaExtraFn read_meta_extra_fn;
    StorageValidateModuleReferencesFn validate_references_fn;
    StorageBindModuleReferencesFn bind_references_fn;
} StorageModuleType;

extern const StorageModuleType g_storage_module_types[];
extern const size_t g_storage_module_type_count;

static inline uint32_t storageEntityShardCount(uint32_t total_count, uint32_t max_per_shard) {
    if (total_count == 0) {
        return 0;
    }
    return (total_count + max_per_shard - 1) / max_per_shard;
}

/** @brief 固定写出一个分片，用于 regions 这类单文件模块。 */
static inline uint32_t storageSingleShardCount(uint32_t total_count, uint32_t max_per_shard) {
    (void)total_count;
    (void)max_per_shard;
    return 1;
}

static inline uint32_t storageModuleShardCount(const StorageModuleType* module_type, uint32_t total_count) {
    if (module_type->shard_count_fn) {
        return module_type->shard_count_fn(total_count, module_type->max_per_shard);
    }
    return storageEntityShardCount(total_count, module_type->max_per_shard);
}

/** @brief 生成分片读写循环；实体字段由 serialize/deserialize 处理，容器和 ID 映射由宏恢复。 */
#define STORAGE_DEFINE_SHARDED_IMPL(FuncName, type_str, EntityType, list_var, id_map, id_map_put, id_map_resize, serialize_fn, deserialize_fn,     \
                                    reserve_min, max_per_shard, EXTRA_LOAD_RESERVE_CODE)                                                           \
    static DOMINO_CODE storageReserveLoad##FuncName(uint32_t total_count) {                                                                        \
        size_t reserve = storageComputeReserveCapacity(total_count, (reserve_min));                                                                \
        STORAGE_RESERVE_ID_MAP(type_str, id_map, id_map_resize, reserve);                                                                          \
        if ((list_var).m < reserve) {                                                                                                              \
            kv_resize(EntityType, list_var, reserve);                                                                                              \
        }                                                                                                                                          \
        do {                                                                                                                                       \
            EXTRA_LOAD_RESERVE_CODE                                                                                                                \
        } while (0);                                                                                                                               \
        return CODE_OK;                                                                                                                            \
    }                                                                                                                                              \
    static DOMINO_CODE storageLoadOne##FuncName(yyjson_val* entity_value) {                                                                        \
        EntityType out_entity = {0};                                                                                                               \
        if (deserialize_fn(entity_value, &out_entity) != 0) {                                                                                      \
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to deserialize " type_str " id=%u",                \
                              (uint32_t)out_entity.id);                                                                                            \
            return ERR_INVALID_DATA;                                                                                                               \
        }                                                                                                                                          \
        size_t old_entity_count = kv_size(list_var);                                                                                               \
        kv_push(EntityType, list_var, out_entity);                                                                                                 \
        uint32_t entity_list_index = (uint32_t)(kv_size(list_var) - 1);                                                                            \
        int absent;                                                                                                                                \
        khint_t map_slot = id_map_put(id_map, out_entity.id, &absent);                                                                             \
        if (absent < 0) {                                                                                                                          \
            (list_var).n = old_entity_count;                                                                                                       \
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to map " type_str " id=%llu",                      \
                              (unsigned long long)out_entity.id);                                                                                  \
            return ERR_MEMORY_ALLOC;                                                                                                               \
        }                                                                                                                                          \
        if (absent == 0) {                                                                                                                         \
            (list_var).n = old_entity_count;                                                                                                       \
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate " type_str " id=%llu",                          \
                              (unsigned long long)out_entity.id);                                                                                  \
            return ERR_ALREADY_EXISTS;                                                                                                             \
        }                                                                                                                                          \
        kh_val(id_map, map_slot) = entity_list_index;                                                                                              \
        return CODE_OK;                                                                                                                            \
    }                                                                                                                                              \
    DOMINO_CODE serdeSave##FuncName(const char* save_path) {                                                                                       \
        uint32_t total = (uint32_t)kv_size(list_var);                                                                                              \
        if (total == 0) {                                                                                                                          \
            return CODE_OK;                                                                                                                        \
        }                                                                                                                                          \
        uint32_t shard_count = storageEntityShardCount(total, (max_per_shard));                                                                    \
        for (uint32_t shard = 0; shard < shard_count; shard++) {                                                                                   \
            uint32_t start = shard * (max_per_shard);                                                                                              \
            uint32_t end = start + (max_per_shard);                                                                                                \
            if (end > total) {                                                                                                                     \
                end = total;                                                                                                                       \
            }                                                                                                                                      \
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);                                                                                     \
            yyjson_mut_val* root = yyjson_mut_obj(doc);                                                                                            \
            yyjson_mut_doc_set_root(doc, root);                                                                                                    \
            yyjson_mut_obj_add_str(doc, root, "type", (type_str));                                                                                 \
            yyjson_mut_obj_add_uint(doc, root, "shard_id", shard);                                                                                 \
            yyjson_mut_val* entities = yyjson_mut_obj_add_arr(doc, root, "entities");                                                              \
            for (uint32_t entity_index = start; entity_index < end; entity_index++) {                                                              \
                yyjson_mut_val* entity_obj = serialize_fn(doc, &kv_A(list_var, entity_index));                                                     \
                if (!entity_obj) {                                                                                                                 \
                    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to serialize " type_str " id=%u",          \
                                      (uint32_t)kv_A(list_var, entity_index).id);                                                                  \
                    yyjson_mut_doc_free(doc);                                                                                                      \
                    return ERR_INVALID_DATA;                                                                                                       \
                }                                                                                                                                  \
                yyjson_mut_arr_append(entities, entity_obj);                                                                                       \
            }                                                                                                                                      \
            DOMINO_CODE result = storageWriteShardFile((type_str), shard, save_path, doc);                                                         \
            if (result != CODE_OK) {                                                                                                               \
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to write " type_str " shard=%u: %s", shard,    \
                                  dominoErrorCodeToString(result));                                                                                \
                yyjson_mut_doc_free(doc);                                                                                                          \
                return result;                                                                                                                     \
            }                                                                                                                                      \
            yyjson_mut_doc_free(doc);                                                                                                              \
        }                                                                                                                                          \
        return CODE_OK;                                                                                                                            \
    }                                                                                                                                              \
    DOMINO_CODE serdeLoad##FuncName(const char* save_path, uint32_t total_count, uint32_t shard_count) {                                           \
        DOMINO_CODE reserve_result = storageReserveLoad##FuncName(total_count);                                                                    \
        if (reserve_result != CODE_OK) {                                                                                                           \
            return reserve_result;                                                                                                                 \
        }                                                                                                                                          \
        for (uint32_t shard = 0; shard < shard_count; shard++) {                                                                                   \
            DOMINO_CODE result = CODE_OK;                                                                                                          \
            yyjson_doc* doc = storageReadShardFile((type_str), shard, save_path, &result);                                                         \
            if (result != CODE_OK) {                                                                                                               \
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to read " type_str " shard=%u: %s", shard,     \
                                  dominoErrorCodeToString(result));                                                                                \
                return result;                                                                                                                     \
            }                                                                                                                                      \
            if (!doc) {                                                                                                                            \
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid " type_str " shard=%u: invalid json", shard); \
                return ERR_INVALID_JSON;                                                                                                           \
            }                                                                                                                                      \
            yyjson_val* root = yyjson_doc_get_root(doc);                                                                                           \
            yyjson_val* entities = serdeValidateShardRoot(root, (type_str), (type_str), shard);                                                    \
            if (!entities) {                                                                                                                       \
                yyjson_doc_free(doc);                                                                                                              \
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid " type_str " shard=%u: invalid shard root",   \
                                  shard);                                                                                                          \
                return ERR_INVALID_JSON;                                                                                                           \
            }                                                                                                                                      \
            size_t array_index;                                                                                                                    \
            size_t array_count;                                                                                                                    \
            yyjson_val* entity_value;                                                                                                              \
            yyjson_arr_foreach(entities, array_index, array_count, entity_value) {                                                                 \
                DOMINO_CODE entity_result = storageLoadOne##FuncName(entity_value);                                                                \
                if (entity_result != CODE_OK) {                                                                                                    \
                    yyjson_doc_free(doc);                                                                                                          \
                    return entity_result;                                                                                                          \
                }                                                                                                                                  \
            }                                                                                                                                      \
            yyjson_doc_free(doc);                                                                                                                  \
        }                                                                                                                                          \
        if ((uint32_t)kv_size(list_var) != total_count) {                                                                                          \
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, type_str " loaded count %zu != meta total_count %u",       \
                              kv_size(list_var), total_count);                                                                                     \
            return ERR_INVALID_DATA;                                                                                                               \
        }                                                                                                                                          \
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_INFO, "loaded %zu " type_str "s", kv_size(list_var));                 \
        return CODE_OK;                                                                                                                            \
    }

#define STORAGE_DEFINE_SHARDED_SIMPLE(FuncName, type_str, EntityType, list_var, id_map, id_map_put, id_map_resize, serialize_fn, deserialize_fn, \
                                      reserve_min, max_per_shard)                                                                                \
    STORAGE_DEFINE_SHARDED_IMPL(FuncName, type_str, EntityType, list_var, id_map, id_map_put, id_map_resize, serialize_fn, deserialize_fn,       \
                                reserve_min, max_per_shard, STORAGE_NO_EXTRA_LOAD_RESERVE)

/** @brief 额外预留 data 数组及其 ID map，具体 deserialize 负责读取和插入 data。 */
#define STORAGE_DEFINE_SHARDED_WITH_DATA(FuncName, type_str, EntityType, list_var, id_map, id_map_put, id_map_resize, serialize_fn, deserialize_fn, \
                                         reserve_min, max_per_shard, DataType, data_list_var, data_id_map, data_id_map_resize)                      \
    STORAGE_DEFINE_SHARDED_IMPL(                                                                                                                    \
        FuncName, type_str, EntityType, list_var, id_map, id_map_put, id_map_resize, serialize_fn, deserialize_fn, reserve_min, max_per_shard,      \
        if ((data_list_var).m < reserve) { kv_resize(DataType, data_list_var, reserve); } STORAGE_RESERVE_ID_MAP(type_str " data", data_id_map,     \
                                                                                                                 data_id_map_resize, reserve);)

#endif
