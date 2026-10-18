#ifndef DOMINO_ENGINE_STORAGE_IO_H
#define DOMINO_ENGINE_STORAGE_IO_H

#include <stddef.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "yyjson/yyjson.h"

#define DOMINO_STORAGE_SHARD_FILE_MAX_BYTES (100u * 1024u * 1024u)

/* 内部 I/O 契约：路径、输出缓冲区、result 和 doc 均有效；type_name 来自固定注册表。
 * JSON 读取成功时返回非空文档，失败时返回 nullptr 并设置 result；调用者只需检查 result。
 */

/**
 * @brief 构建指定类型、指定分片的 JSON 文件路径。
 *
 * regions 是固定单文件 `regions.json`；其他模块使用稳定 type_name 作为文件名前缀。
 * @return 成功返回 CODE_OK，路径格式化失败返回 ERR_STRING_FORMAT，超出缓冲区时返回 ERR_BUFFER_TOO_SMALL。
 */
DOMINO_CODE storageBuildShardPath(char* buffer, size_t buffer_size, const char* save_path, const char* type_name, uint32_t shard_id);

/** @brief 计算 Castagnoli CRC32C，用于存档完整性校验。 */
uint32_t storageCrc32c(const void* data, size_t len);

/** @brief 读取并解析 JSON 文件，可选按预期 CRC32C 和字节数校验。 */
yyjson_doc* storageReadJsonFile(const char* path, bool do_verify, uint32_t expect_crc32c, uint32_t expect_size, DOMINO_CODE* result);

/** @brief 原子写出 JSON 文件；do_verify 为 true 时必须提供两个摘要输出地址，否则允许传 nullptr。 */
DOMINO_CODE storageWriteJsonFile(const char* path, yyjson_mut_doc* doc, bool do_verify, uint32_t* out_crc32c, uint32_t* out_size);

/** @brief 写模块分片文件，并在开启完整性校验时登记该分片摘要。 */
DOMINO_CODE storageWriteShardFile(const char* type_name, uint32_t shard_id, const char* save_path, yyjson_mut_doc* doc);

/** @brief 读取模块分片文件；调用前须完成全部模块 meta 加载，shard_id 必须位于该模块已登记范围内。 */
yyjson_doc* storageReadShardFile(const char* type_name, uint32_t shard_id, const char* save_path, DOMINO_CODE* result);

#endif
