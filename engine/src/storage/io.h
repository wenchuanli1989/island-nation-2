#ifndef DOMINO_ENGINE_STORAGE_IO_H
#define DOMINO_ENGINE_STORAGE_IO_H

#include <stddef.h>
#include <stdint.h>

#include "yyjson/yyjson.h"

#define DOMINO_STORAGE_SHARD_FILE_MAX_BYTES (100u * 1024u * 1024u)

/** @file io.h
 * @brief 存档文件读写：分片路径、CRC32C、JSON 读写及分片摘要登记。IO、格式、校验或分配失败均致命退出。
 */

/** @brief 分片文件路径：regions 固定为 regions.json，其他模块为 {type_name}_{shard_id}.json。 */
void storageBuildShardPath(char* buffer, size_t buffer_size, const char* save_path, const char* type_name, uint32_t shard_id);

/** @brief Castagnoli CRC32C。 */
uint32_t storageCalcCrc32c(const void* data, size_t len);

/** @brief 读取并解析 JSON 文件；integrity_check 时先核对字节数和 CRC32C。 */
yyjson_doc* storageReadJsonFile(const char* path, bool integrity_check, uint32_t expect_crc32c, uint32_t expect_size);

/** @brief 原子写出 JSON 文件；integrity_check 时输出所写字节的 CRC32C 和大小，否则两个输出可为空。 */
void storageWriteJsonFile(const char* path, yyjson_mut_doc* doc, bool integrity_check, uint32_t* out_crc32c, uint32_t* out_size);

/** @brief 写一个分片文件；开启完整性校验时把摘要登记到清单。 */
void storageWriteShardFile(const char* type_name, uint32_t shard_id, const char* save_path, yyjson_mut_doc* doc);

/** @brief 读取一个分片文件；开启完整性校验时按清单中的摘要核对。
 * @pre 全部模块 meta 已加载，shard_id 在该模块已登记的范围内。
 */
yyjson_doc* storageReadShardFile(const char* type_name, uint32_t shard_id, const char* save_path);

#endif
