#ifndef DOMINO_SHARED_COMMON_H
#define DOMINO_SHARED_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "domino_shared_error_codes.h"

/**
 * @brief 将当前 UTC 时间格式化为 `YYYY-MM-DDTHH:MM:SS.mmmZ` 写入缓冲区。
 * @param out_ptr 输出缓冲；非空且容量非零时，失败保持空字符串。
 * @param out_len 缓冲区长度（含结尾空字符）
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；时钟采样失败返回 `ERR_CLOCK_READ`；
 * 日期转换或字符串格式化失败返回 `ERR_STRING_FORMAT`；缓冲区不足返回 `ERR_BUFFER_TOO_SMALL`。
 */
DOMINO_CODE dominoFormatUtcTimestampNow(char* out_ptr, size_t out_len);

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
 * @return `CODE_OK` 成功；空指针/空路径分别返回 `ERR_NULL_POINTER`/`ERR_INVALID_PARAM`；路径超出内部缓冲返回 `ERR_BUFFER_TOO_SMALL`；
 * 创建目录失败返回 `ERR_DIRECTORY_CREATE`；查询路径失败返回 `ERR_FILE_STAT`；已有路径不是目录返回 `ERR_PATH_TYPE`。
 */
DOMINO_CODE dominoMkdirRecursive(const char* path);

/**
 * @brief 64 位 UTC 墙上时间戳毫秒，用于日志与文件命名；`timespec_get` 失败时退化为秒级精度。
 * @note 不受引擎冻结控制，不是累计未冻结现实毫秒，不能用于任务、timer 或收益结算的业务计时。
 */
uint64_t dominoWallTimeMs(void);

/**
 * @brief 采样 64 位 UTC 墙上时间戳毫秒，供存档生命周期文件命名使用；失败不降级或重试。
 * @param wall_timestamp_ms_ptr 成功时写入 UTC 毫秒；失败时保持原值。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；采样失败返回 ERR_CLOCK_READ。
 * @note 不是累计未冻结现实毫秒，不能用于业务计时。
 */
DOMINO_CODE dominoWallTimeMsChecked(uint64_t* wall_timestamp_ms_ptr);

/** @brief 单调时间纳秒；只用于计算间隔，不可转换为 UTC 时间。 */
uint64_t dominoMonotonicTimeNs(void);

/**
 * @brief 采样单调时间纳秒，供生命周期初始化使用；失败不降级或重试。
 * @param monotonic_ns_ptr 成功时写入单调纳秒；失败时保持原值。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；采样失败返回 ERR_CLOCK_READ。
 */
DOMINO_CODE dominoMonotonicTimeNsChecked(uint64_t* monotonic_ns_ptr);

/** @brief 当前进程 ID，用于临时文件和目录命名。 */
uint64_t dominoGetPid(void);

/**
 * @brief 将标准库 FILE* 的缓冲刷出并通过 `fsync(fileno(file_ptr))` 尽力落盘。
 * @return 0 成功；非 0 失败。
 */
int dominoFlushFile(FILE* file_ptr);

/**
 * @brief 递归删除文件或目录；路径不存在时视为成功，遇到符号链接时只删除链接本身。
 * @return `CODE_OK` 成功；失败保留具体的参数、缓冲区、内存、路径查询、目录遍历或删除错误码。
 * @note 存档生命周期专用；失败立即返回，不释放失败时持有的资源，由宿主结束进程。
 */
DOMINO_CODE dominoRemovePathRecursive(const char* path);

/**
 * @brief 以二进制只读方式读入整个文件到堆缓冲区。
 * @param path 文件路径。
 * @param length 成功时写入字节数；失败时置 0。
 * @param result 成功时写入 `CODE_OK`；失败时写入对应错误码。当前实现要求该指针非空。
 * @return 成功时返回堆缓冲区（空文件时为 nullptr）；失败时返回 nullptr。非空返回值需由调用方 `free`。
 * @note 存档生命周期专用；失败保留打开文件和已分配缓冲区，由宿主结束进程。
 */
uint8_t* dominoReadFileBytes(const char* path, size_t* length, DOMINO_CODE* result);

/**
 * @brief 以“同目录临时文件 + rename”的方式写入目标文件，避免出现半写入文件。
 * @param path 目标文件路径。
 * @param data 待写入内容；当前实现要求非 NULL。
 * @param len 待写入字节数。
 * @return `CODE_OK` 成功；时钟采样失败返回 `ERR_CLOCK_READ`；关闭失败返回 `ERR_FILE_CLOSE`；否则返回文件/参数相关错误码。
 * @note 存档生命周期专用；失败保留临时文件和打开的文件，不清理或重试，由宿主结束进程。
 */
DOMINO_CODE dominoAtomicWriteFile(const char* path, const void* data, size_t len);

/**
 * @brief 按 POSIX 分隔符 '/' 将路径拆分为 parent/base 两部分，忽略末尾分隔符。
 * @param path 输入路径。
 * @param parent 输出父目录缓冲区。
 * @param parent_cap parent 缓冲区容量（包含 '\0'）。
 * @param base 输出 basename 缓冲区。
 * @param base_cap base 缓冲区容量（包含 '\0'）。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；输入路径非法返回 `ERR_INVALID_PARAM`；缓冲区不足返回 `ERR_BUFFER_TOO_SMALL`。
 */
DOMINO_CODE dominoPathSplitParentBase(const char* path, char* parent, size_t parent_cap, char* base, size_t base_cap);

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
