#ifndef DOMINO_ENGINE_STORAGE_MANIFEST_H
#define DOMINO_ENGINE_STORAGE_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @file manifest.h
 * @brief 存档清单：一次加载或保存期间的模块 meta、分片摘要（CRC32C/size）和完整性校验开关。
 * @note 清单只在 storage 入口的一次加载或保存内有效，由 Init 建立、Destroy 清空；不跨轮保留。
 */

#define STORAGE_MODULE_COUNT_MAX 64
#define STORAGE_TYPE_NAME_SIZE 32
#define STORAGE_SHARD_DIGEST_INITIAL_CAPACITY 64u

/** @brief 一个模块的 meta：自身摘要（记在根 meta 中）和实体数、分片数。 */
typedef struct {
    uint32_t crc32c;
    uint32_t size;
    uint32_t total_count;
    uint32_t shard_count;
    char type_name[STORAGE_TYPE_NAME_SIZE];
} StorageModuleMeta;

/** @brief 一个分片文件的摘要：保存时计算，加载时从模块 meta 恢复。 */
typedef struct {
    char type_name[STORAGE_TYPE_NAME_SIZE];
    uint32_t shard_id;
    uint32_t crc32c;
    uint32_t size;
} StorageShardDigest;

/** @brief 为本轮加载或保存分配分片摘要表。 */
void storageManifestInit(void);

/** @brief 释放并清空本轮清单。 */
void storageManifestDestroy(void);

void storageManifestSetIntegrityCheck(bool integrity_check);

/** @brief 本轮是否校验或记录分片摘要。 */
bool storageManifestChecksIntegrity(void);

/** @brief 登记一个模块 meta；每轮每个注册类型只登记一次。 */
void storageManifestAddModule(StorageModuleMeta module_meta);

const StorageModuleMeta* storageManifestFindModule(const char* type_name);

/** @brief 登记一个分片摘要；每个注册类型按 shard_id 递增各登记一次。 */
void storageManifestAddShard(StorageShardDigest shard_digest);

const StorageShardDigest* storageManifestFindShard(const char* type_name, uint32_t shard_id);

#endif
