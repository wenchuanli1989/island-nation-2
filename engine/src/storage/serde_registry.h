#ifndef DOMINO_ENGINE_STORAGE_SERDE_REGISTRY_H
#define DOMINO_ENGINE_STORAGE_SERDE_REGISTRY_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_common.h"
#include "domino_shared_error_codes.h"
#include "io.h"
#include "klib/khashl.h"

#define DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA 8000
#define DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA 100000
#define DOMINO_STORAGE_SHARD_HUMAN_TASK 4096
#define STORAGE_KHASHL_MAX_BUCKETS ((size_t)1U << 31)
#define STORAGE_KHASHL_MAX_LOAD_COUNT (STORAGE_KHASHL_MAX_BUCKETS - (STORAGE_KHASHL_MAX_BUCKETS >> 2))

/** @brief 校验加载数量不会让数组字节数溢出或超过映射表容量，不预分配。 */
static inline void storageValidateLoadCount(uint32_t total_count, size_t element_size) {
    if ((size_t)total_count > SIZE_MAX / element_size) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "restored entity allocation size overflows (count=%u, element_size=%zu)", total_count,
                               element_size);
    }
    if ((size_t)total_count > STORAGE_KHASHL_MAX_LOAD_COUNT) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "restored entity count exceeds ID map capacity (count=%u, capacity=%zu)", total_count,
                               STORAGE_KHASHL_MAX_LOAD_COUNT);
    }
}

typedef uint32_t (*StorageShardCountFn)(uint32_t total_count, uint32_t max_per_shard);
/** @brief 向模块 meta 写入扩展字段。 */
typedef void (*StorageWriteModuleMetaExtraFn)(yyjson_mut_doc* doc, yyjson_mut_val* root, const char* type_name);
/** @brief 从模块 meta 读取扩展字段。 */
typedef void (*StorageReadModuleMetaExtraFn)(yyjson_val* root, const char* type_name);

/** @brief 全部模块加载后，按持久化 ID 绑定运行时指针；不分配资源、不会失败。 */
typedef void (*StorageBindModuleReferencesFn)(void);

/** @brief 一个存档模块的注册项：文件名前缀、分片规则和各回调；regions 为单文件，其余为 {type_name}_{shard_id}.json。 */
typedef struct {
    const char* type_name;
    size_t (*count_fn)(void);
    StorageShardCountFn shard_count_fn;
    void (*save_fn)(const char* save_path);
    void (*load_fn)(const char* save_path, uint32_t total, uint32_t shard_count);
    StorageWriteModuleMetaExtraFn write_meta_extra_fn;
    StorageReadModuleMetaExtraFn read_meta_extra_fn;
    StorageBindModuleReferencesFn bind_references_fn;
    uint32_t max_per_shard;
} StorageModuleDescriptor;

extern const StorageModuleDescriptor g_storage_module_registry[];
extern const size_t g_storage_module_registry_count;

/** @brief 按每分片实体上限计算分片数。 */
static inline uint32_t storageEntityShardCount(uint32_t total_count, uint32_t max_per_shard) {
    return total_count / max_per_shard + (total_count % max_per_shard != 0);
}

/** @brief 单文件模块（regions）固定一个分片。 */
static inline uint32_t storageSingleShardCount(uint32_t total_count, uint32_t max_per_shard) {
    (void)total_count;
    (void)max_per_shard;
    return 1;
}

static inline uint32_t storageModuleShardCount(const StorageModuleDescriptor* module_ptr, uint32_t total_count) {
    return module_ptr->shard_count_fn(total_count, module_ptr->max_per_shard);
}

/** @brief 为扁平容器实体生成 serdeSave##FuncName / serdeLoad##FuncName：按分片写出，加载时逐个反序列化（实体先清零）、
 * 追加到容器并登记 ID 映射。EXTRA_LOAD_VALIDATE_CODE 在加载前执行，EXTRA_LOAD_BIND_CODE 在全部分片加载后执行。
 */
#define STORAGE_DEFINE_SHARDED_IMPL(FuncName, type_str, EntityType, list_var, id_map, id_map_put, serialize_fn, deserialize_fn, max_per_shard,      \
                                    EXTRA_LOAD_VALIDATE_CODE, EXTRA_LOAD_BIND_CODE)                                                                 \
    static void storageLoadOne##FuncName(yyjson_val* entity_value) {                                                                                \
        EntityType out_entity = {0};                                                                                                                \
        deserialize_fn(entity_value, &out_entity);                                                                                                  \
        int absent;                                                                                                                                 \
        khint_t map_slot = id_map_put(id_map, out_entity.id, &absent);                                                                              \
        if (absent < 0) {                                                                                                                           \
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to insert restored entity ID (type=%s, id=%" PRIu64 ")", (type_str),                   \
                                   (uint64_t)out_entity.id);                                                                                        \
        }                                                                                                                                           \
        uint32_t entity_list_index = (uint32_t)kv_size(list_var);                                                                                   \
        kv_push(EntityType, list_var, out_entity);                                                                                                  \
        kh_val(id_map, map_slot) = entity_list_index;                                                                                               \
    }                                                                                                                                               \
    void serdeSave##FuncName(const char* save_path) {                                                                                               \
        uint32_t total = (uint32_t)kv_size(list_var);                                                                                               \
        uint32_t shard_count = storageEntityShardCount(total, (max_per_shard));                                                                     \
        for (uint32_t shard = 0; shard < shard_count; shard++) {                                                                                    \
            uint32_t start = shard * (max_per_shard);                                                                                               \
            uint32_t end = start + (max_per_shard);                                                                                                 \
            if (end > total) {                                                                                                                      \
                end = total;                                                                                                                        \
            }                                                                                                                                       \
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);                                                                                      \
            if (!doc) {                                                                                                                             \
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate shard JSON document (type=%s, shard=%u)", (type_str), shard);          \
            }                                                                                                                                       \
            yyjson_mut_val* root = yyjson_mut_obj(doc);                                                                                             \
            if (!root) {                                                                                                                            \
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate shard JSON root (type=%s, shard=%u)", (type_str), shard);              \
            }                                                                                                                                       \
            yyjson_mut_doc_set_root(doc, root);                                                                                                     \
            if (!yyjson_mut_obj_add_str(doc, root, "type", (type_str)) || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard)) {                 \
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate shard JSON identity fields (type=%s, shard=%u)", (type_str), shard);   \
            }                                                                                                                                       \
            yyjson_mut_val* entities = yyjson_mut_obj_add_arr(doc, root, "entities");                                                               \
            if (!entities) {                                                                                                                        \
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate shard JSON entity array (type=%s, shard=%u)", (type_str), shard);      \
            }                                                                                                                                       \
            for (uint32_t entity_index = start; entity_index < end; entity_index++) {                                                               \
                yyjson_mut_val* entity_obj = serialize_fn(doc, &kv_A(list_var, entity_index));                                                      \
                if (!yyjson_mut_arr_append(entities, entity_obj)) {                                                                                 \
                    DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to append shard JSON entity (type=%s, shard=%u, index=%u)", (type_str), shard, \
                                           entity_index);                                                                                           \
                }                                                                                                                                   \
            }                                                                                                                                       \
            storageWriteShardFile((type_str), shard, save_path, doc);                                                                               \
            yyjson_mut_doc_free(doc);                                                                                                               \
        }                                                                                                                                           \
    }                                                                                                                                               \
    void serdeLoad##FuncName(const char* save_path, uint32_t total_count, uint32_t shard_count) {                                                   \
        storageValidateLoadCount(total_count, sizeof(EntityType));                                                                                  \
        do {                                                                                                                                        \
            EXTRA_LOAD_VALIDATE_CODE                                                                                                                \
        } while (0);                                                                                                                                \
        for (uint32_t shard = 0; shard < shard_count; shard++) {                                                                                    \
            yyjson_doc* doc = storageReadShardFile((type_str), shard, save_path);                                                                   \
            yyjson_val* root = yyjson_doc_get_root(doc);                                                                                            \
            yyjson_val* entities = yyjson_obj_get(root, "entities");                                                                                \
            size_t array_index;                                                                                                                     \
            size_t array_count;                                                                                                                     \
            yyjson_val* entity_value;                                                                                                               \
            yyjson_arr_foreach(entities, array_index, array_count, entity_value) {                                                                  \
                storageLoadOne##FuncName(entity_value);                                                                                             \
            }                                                                                                                                       \
            yyjson_doc_free(doc);                                                                                                                   \
        }                                                                                                                                           \
        do {                                                                                                                                        \
            EXTRA_LOAD_BIND_CODE                                                                                                                    \
        } while (0);                                                                                                                                \
    }

#define STORAGE_DEFINE_SHARDED_SIMPLE(FuncName, type_str, EntityType, list_var, id_map, id_map_put, serialize_fn, deserialize_fn, max_per_shard) \
    STORAGE_DEFINE_SHARDED_IMPL(                                                                                                                 \
        FuncName, type_str, EntityType, list_var, id_map, id_map_put, serialize_fn, deserialize_fn, max_per_shard, ((void)0);, ((void)0);)

/** @brief 带扩展 data 的实体：全部分片加载后再绑定 data 指针，避免容器扩容使已绑定地址失效。 */
#define STORAGE_DEFINE_SHARDED_WITH_DATA(FuncName, type_str, EntityType, list_var, id_map, id_map_put, serialize_fn, deserialize_fn, max_per_shard, \
                                         DataType, data_get_fn)                                                                                     \
    STORAGE_DEFINE_SHARDED_IMPL(                                                                                                                    \
        FuncName, type_str, EntityType, list_var, id_map, id_map_put, serialize_fn, deserialize_fn, max_per_shard,                                  \
        storageValidateLoadCount(total_count, sizeof(DataType));                                                                                    \
        , for (size_t entity_index = 0U; entity_index < kv_size(list_var); entity_index++) {                                                        \
            EntityType* entity_ptr = &kv_A(list_var, entity_index);                                                                                 \
            if (entity_ptr->data_id != 0U) {                                                                                                        \
                entity_ptr->data = data_get_fn(entity_ptr->data_id);                                                                                \
            }                                                                                                                                       \
        })

#endif
