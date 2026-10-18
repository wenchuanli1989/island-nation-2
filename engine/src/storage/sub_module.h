#ifndef DOMINO_ENGINE_STORAGE_SUB_MODULE_H
#define DOMINO_ENGINE_STORAGE_SUB_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"

#define STORAGE_SUB_MODULE_MAX_COUNT 64

#define STORAGE_SUB_MODULE_TYPE_MAX_LENGTH 32

#define STORAGE_SUB_MODULE_SHARD_RESERVE_MIN_COUNT 64u

/** @brief 加载时使用的模块 meta；文件名由 type_name 推导，摘要由根 meta 保存。 */
typedef struct {
    uint32_t crc32c;
    uint32_t size;

    uint32_t total_count;
    uint32_t shard_count;
    char type_name[STORAGE_SUB_MODULE_TYPE_MAX_LENGTH];
} StorageSubModuleMeta;

/** @brief 分片摘要：保存时计算，加载时从模块 meta 恢复。 */
typedef struct {
    char type_name[STORAGE_SUB_MODULE_TYPE_MAX_LENGTH];
    uint32_t shard_id;
    uint32_t crc32c;
    uint32_t size;
} StorageSubModuleShard;

/** @brief 清空本轮加载或保存使用的模块 meta 和分片摘要缓存。 */
DOMINO_CODE storageSubModuleReset(void);

void storageSubModuleSetIntegrityVerify(bool integrity_verify);

bool storageSubModuleIntegrityVerify(void);

/** @brief 释放本轮 load/save 会话的全部临时索引。 */
void storageSubModuleDestroy(void);

/** @brief 加入已解析的模块记录；调用方按固定注册表遍历，每轮每个类型只加入一次，名称已终止。 */
DOMINO_CODE storageSubModuleMetaAdd(StorageSubModuleMeta meta);

const StorageSubModuleMeta* storageSubModuleMetaFind(const char* type_name);

/** @brief 加入分片摘要；调用方保证名称已终止，且每个注册类型按递增 shard_id 只加入一次。 */
DOMINO_CODE storageSubModuleShardAdd(StorageSubModuleShard shard);

const StorageSubModuleShard* storageSubModuleShardFind(const char* type_name, uint32_t shard_id);

#endif
