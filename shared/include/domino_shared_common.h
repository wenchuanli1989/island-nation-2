#ifndef DOMINO_SHARED_COMMON_H
#define DOMINO_SHARED_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "domino_shared_error_codes.h"

/**
 * @brief 将当前 UTC 时间格式化为 `YYYY-MM-DDTHH:MM:SS.mmmZ` 写入缓冲区。
 * @param out_ptr 输出缓冲；失败时可能写入全零占位 `0000-00-00T00:00:00.000Z`。
 * @param out_len 缓冲区长度（含结尾空字符）
 */
void dominoFormatUtcTimestampNow(char* out_ptr, size_t out_len);

/**
 * @brief 创建单级目录。
 *
 * 当前 Linux 实现直接调用 `mkdir(path, 0755)`；不会递归创建父目录。
 *
 * @return 与 `mkdir(2)` 成功/失败约定一致（成功为 0）。
 */
int dominoMkdir(const char* path);

/**
 * @brief 递归创建目录树；目标目录已存在时视为成功。
 *
 * @return `CODE_OK` 成功；路径非法返回 `ERR_INVALID_PARAM`；创建失败返回 `ERR_FILE_WRITE`。
 */
DOMINO_CODE dominoMkdirRecursive(const char* path);

/** @brief UTC 墙上时间毫秒；`timespec_get` 失败时退化为秒级精度。 */
uint64_t dominoWallTimeMs(void);

/** @brief 单调时间纳秒；只用于计算间隔，不可转换为 UTC 时间。 */
uint64_t dominoMonotonicTimeNs(void);

/** @brief 当前进程 ID，用于临时文件和目录命名。 */
uint64_t dominoGetPid(void);

/**
 * @brief 将标准库 FILE* 的缓冲刷出并通过 `fsync(fileno(file_ptr))` 尽力落盘。
 * @return 0 成功；非 0 失败。
 */
int dominoFlushFile(FILE* file_ptr);

/** @brief 递归删除文件或目录；路径不存在时视为成功，遇到符号链接时只删除链接本身。 */
int dominoRemovePathRecursive(const char* path);

/**
 * @brief 以二进制只读方式读入整个文件到堆缓冲区。
 * @param path 文件路径。
 * @param length 成功时写入字节数；失败时置 0。
 * @param result 成功时写入 `CODE_OK`；失败时写入对应错误码。当前实现要求该指针非空。
 * @return 成功时返回堆缓冲区（空文件时为 nullptr）；失败时返回 nullptr。非空返回值需由调用方 `free`。
 */
uint8_t* dominoReadFileBytes(const char* path, size_t* length, DOMINO_CODE* result);

/**
 * @brief 以“同目录临时文件 + rename”的方式写入目标文件，避免出现半写入文件。
 * @param path 目标文件路径。
 * @param data 待写入内容；当前实现要求非 NULL。
 * @param len 待写入字节数。
 * @return `CODE_OK` 成功；否则返回文件/参数相关错误码。
 */
int dominoAtomicWriteFile(const char* path, const void* data, size_t len);

/**
 * @brief 将路径拆分为 parent/base 两部分（兼容 '/' 与 '\\'；会忽略末尾分隔符）。
 * @param path 输入路径。
 * @param parent 输出父目录缓冲区。
 * @param parent_cap parent 缓冲区容量（包含 '\0'）。
 * @param base 输出 basename 缓冲区。
 * @param base_cap base 缓冲区容量（包含 '\0'）。
 * @return 0 成功；非 0 失败（输入非法或缓冲区不足）。
 */
int dominoPathSplitParentBase(const char* path, char* parent, size_t parent_cap, char* base, size_t base_cap);

/**
 * @brief 判断路径是否存在。
 * @param path 待检查路径。
 * @param directory_only true 时仅当路径存在且为目录时返回 1；false 时文件或目录存在即返回 1。
 * @return 1 存在且类型匹配；0 不存在或类型不匹配。
 */
int dominoPathExists(const char* path, bool directory_only);

/** @brief 当前 Linux 实现下的 `rename(2)` 封装。 */
int dominoRename(const char* tmp_path, const char* final_path);
#endif
