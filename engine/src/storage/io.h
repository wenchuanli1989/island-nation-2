#ifndef DOMINO_ENGINE_STORAGE_IO_H
#define DOMINO_ENGINE_STORAGE_IO_H

#include <stddef.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "yyjson/yyjson.h"

#define DOMINO_STORAGE_SHARD_FILE_MAX_BYTES (100u * 1024u * 1024u)

/**
 * @brief 构建指定类型、指定分片的 JSON 文件路径。
 *
 * regions 是固定单文件 `regions.json`；其他模块使用稳定 type_name 作为文件名前缀。
 */
void storageBuildShardPath(char* buffer, size_t buffer_size, const char* save_path, const char* type_name, uint32_t shard_id);

/** @brief 计算 Castagnoli CRC32C，用于存档完整性校验。 */
uint32_t storageCrc32c(const void* data, size_t len);

/** @brief 读取并解析 JSON 文件，可选按预期 CRC32C 和字节数校验。 */
yyjson_doc* storageReadJsonFile(const char* path, bool do_verify, uint32_t expect_crc32c, uint32_t expect_size, DOMINO_CODE* result);

/** @brief 原子写出 JSON 文件，可选返回整文件 CRC32C 和字节数。 */
DOMINO_CODE storageWriteJsonFile(const char* path, yyjson_mut_doc* doc, bool do_verify, uint32_t* out_crc32c, uint32_t* out_size);

/** @brief 写模块分片文件，并在开启完整性校验时登记该分片摘要。 */
DOMINO_CODE storageWriteShardFile(const char* type_name, uint32_t shard_id, const char* save_path, yyjson_mut_doc* doc);

/** @brief 读取模块分片文件，并在开启完整性校验时使用模块 meta 中的摘要校验。 */
yyjson_doc* storageReadShardFile(const char* type_name, uint32_t shard_id, const char* save_path, DOMINO_CODE* result);

#endif
