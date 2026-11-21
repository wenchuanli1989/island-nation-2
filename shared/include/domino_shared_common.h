#ifndef DOMINO_SHARED_COMMON_H
#define DOMINO_SHARED_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>

#include "domino_shared_error_codes.h"

/** @brief 把错误及来源直接写到 stderr，然后 exit(EXIT_FAILURE)。
 * @note 不经日志队列；exit 仍执行退出回调和标准 I/O 刷新，输出失败不再报错。
 */
[[noreturn]] void dominoExitWithErrorAt(DOMINO_CODE code, const char* file_str, int line, const char* function_str, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));

#define DOMINO_EXIT_WITH_ERROR(code, ...) dominoExitWithErrorAt((code), __FILE__, __LINE__, __func__, __VA_ARGS__)

/** @brief 标准线程接口返回非 thrd_success 时，带原始结果码致命退出。
 * @note thrd_timedout、thrd_busy 等允许的结果须由调用方先行处理。
 */
static inline void dominoRequireThreadSuccessAt(int result_code, DOMINO_CODE code, const char* what_str, const char* file_str, int line,
                                                const char* function_str) {
    if (result_code == thrd_success) {
        return;
    }
    dominoExitWithErrorAt(code, file_str, line, function_str, "%s, result_code=%d", what_str, result_code);
}

#define DOMINO_REQUIRE_THREAD_SUCCESS(result_code, code, what_str) \
    dominoRequireThreadSuccessAt((result_code), (code), (what_str), __FILE__, __LINE__, __func__)

/** @brief 把调用位置和正文直接写到 stdout 并刷新，不截断、不退出；fmt 为空时无操作。
 * @note 用于生命周期等非致命诊断，不依赖日志实例；输出失败不再报错。
 */
void dominoPrintMessageAt(const char* file_str, int line, const char* function_str, const char* fmt, ...) __attribute__((format(printf, 4, 5)));

#define DOMINO_PRINT_MESSAGE(...) dominoPrintMessageAt(__FILE__, __LINE__, __func__, __VA_ARGS__)

/** @brief 把当前 UTC 时间格式化为 `YYYY-MM-DDTHH:MM:SS.mmmZ`（24 字符），年份限 0000～9999。
 * @param out_len 含结尾 NUL 的缓冲长度，至少 25；失败时输出为空串。
 * @return 空指针 ERR_NULL_POINTER；年份越界 ERR_OUT_OF_RANGE；缓冲不足 ERR_BUFFER_TOO_SMALL；日期转换或格式化失败 ERR_STRING_FORMAT。
 * @note 时钟采样失败致命退出。
 */
DOMINO_CODE dominoFormatUtcTimestampNow(char* out_ptr, size_t out_len);

/** @brief 以 0755 创建单级目录（POSIX mkdir，受 umask 影响）。
 * @return 与 mkdir(2) 一致，成功为 0。
 */
int dominoMkdir(const char* path);

/** @brief 递归创建目录树，已存在视为成功。
 * @return 空指针/空路径 ERR_NULL_POINTER/ERR_INVALID_PARAM；路径过长 ERR_BUFFER_TOO_SMALL；创建失败 ERR_DIRECTORY_CREATE；
 *         查询失败 ERR_FILE_STAT；已有路径不是目录 ERR_PATH_TYPE。
 */
DOMINO_CODE dominoMkdirRecursive(const char* path);

/** @brief 当前 UTC 墙上时间毫秒，只用于文件命名；不是业务时间。采样失败致命退出。 */
uint64_t dominoGetWallTimeMs(void);

/** @brief 当前单调时钟纳秒，只用于计算间隔；业务计时须用 time 模块。采样失败致命退出。 */
uint64_t dominoGetMonotonicTimeNs(void);

/** @brief 当前进程 ID（POSIX getpid），用于临时路径命名。 */
uint64_t dominoGetPid(void);

/** @brief 以二进制追加模式打开文件；路径为空表示未配置，返回空指针。打开失败致命退出。 */
FILE* dominoOpenAppendFile(const char* file_path);

/** @brief fflush 后 fclose 调用方拥有的输出文件，不 fsync；不得传入标准输出流。空指针或失败致命退出。 */
void dominoFlushAndCloseFile(FILE* file_ptr);

/** @brief fflush 后 fsync，把文件数据同步到存储设备。
 * @return 0 成功，非 0 失败。
 */
int dominoSyncFile(FILE* file_ptr);

/** @brief 递归删除文件或目录；不存在视为成功，符号链接只删除链接本身，忽略非根路径的末尾 '/'。
 * @return 空指针/空路径 ERR_NULL_POINTER/ERR_INVALID_PARAM；查询失败 ERR_FILE_STAT；目录不可读 ERR_DIRECTORY_OPEN；
 *         删除失败 ERR_FILE_REMOVE；其他遍历失败 ERR_DIRECTORY_READ。
 * @note 失败即停止，不回滚已删除的部分；遍历内部分配失败致命退出。
 */
DOMINO_CODE dominoRemovePathRecursive(const char* path);

/** @brief 把整个文件读入堆缓冲区。
 * @param data_ptr 输出缓冲，成功时由调用方 free；空文件时为空指针。
 * @return 空指针 ERR_NULL_POINTER；空路径 ERR_INVALID_PARAM；打开、读取、关闭失败分别为 ERR_FILE_OPEN、ERR_FILE_READ、ERR_FILE_CLOSE。
 *         失败时两个输出保持空指针和 0。
 * @note 分配失败致命退出。
 */
DOMINO_CODE dominoReadFileBytes(const char* path, uint8_t** data_ptr, size_t* length_ptr);

/** @brief 先写同目录临时文件（mkstemp，权限 0600）并 fsync，再 rename 覆盖目标，避免出现半写入文件。
 * @param len 须大于 0。
 * @return 空指针 ERR_NULL_POINTER；空路径或零长度 ERR_INVALID_PARAM；创建临时文件 ERR_FILE_OPEN；写入、同步、关闭、重命名失败分别为
 *         ERR_FILE_WRITE、ERR_FILE_FLUSH、ERR_FILE_CLOSE、ERR_FILE_RENAME。失败时尽力删除临时文件。
 */
DOMINO_CODE dominoWriteFileAtomic(const char* path, const void* data, size_t len);

/** @brief 按 '/' 把路径拆成父目录和末级名，忽略末尾分隔符；没有分隔符时父目录为 "."。
 * @param parent_cap/base_cap 含结尾 NUL 的缓冲容量。
 * @return 空指针 ERR_NULL_POINTER；路径为空或全为 '/' 时 ERR_INVALID_PARAM；缓冲不足 ERR_BUFFER_TOO_SMALL。
 */
DOMINO_CODE dominoPathSplitParentBase(const char* path, char* parent, size_t parent_cap, char* base, size_t base_cap);

/** @brief rename(source_path, target_path)；存档提交依赖 POSIX 在同一文件系统内原子替换已有目标。
 * @return 0 成功，非 0 失败。
 */
int dominoRename(const char* source_path, const char* target_path);
#endif
