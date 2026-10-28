#ifndef DOMINO_SHARED_COMMON_H
#define DOMINO_SHARED_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>

#include "domino_shared_error_codes.h"

/**
 * @brief 直接向 stderr 打印错误及来源，然后调用 exit(EXIT_FAILURE) 终止整个进程。
 * @param code 错误码，仅用于诊断输出。
 * @param file_str 来源文件名，须为非空、NUL 结尾字符串。
 * @param line 来源行号。
 * @param function_str 来源函数名，须为非空、NUL 结尾字符串。
 * @param fmt 错误原因格式字符串，须为非空、NUL 结尾字符串。
 * @note 不进入日志队列；允许 exit 执行退出回调和标准 I/O 刷新，诊断输出失败不递归报错。
 */
[[noreturn]] void dominoExitWithErrorAt(DOMINO_CODE code, const char* file_str, int line, const char* function_str, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));

#define DOMINO_EXIT_WITH_ERROR(code, ...) dominoExitWithErrorAt((code), __FILE__, __LINE__, __func__, __VA_ARGS__)

/**
 * @brief 要求标准线程接口调用成功，否则输出原始结果及来源并终止进程。
 * @param result_code 标准线程接口的原始返回值。
 * @param code 项目错误码，仅用于诊断输出。
 * @param what_str 操作说明，须为非空、NUL 结尾字符串。
 * @param file_str 调用处文件名。
 * @param line 调用处行号。
 * @param function_str 调用处函数名。
 * @note 仅适用于以 thrd_success 表示成功的接口；调用方须先处理允许的 thrd_timedout、thrd_busy 等正常结果。
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

/**
 * @brief 直接向 stdout 打印调用位置和完整正文，追加换行并刷新，不终止进程。
 * @param file_str 来源文件名；nullptr 时输出问号。
 * @param line 来源行号；非正数按 0 输出。
 * @param function_str 来源函数名；nullptr 时输出问号。
 * @param fmt 正文格式字符串；nullptr 时无操作。
 * @note 不依赖日志实例或队列，不使用定长缓冲或截断；输出失败不递归诊断或退出。
 */
void dominoPrintMessageAt(const char* file_str, int line, const char* function_str, const char* fmt, ...) __attribute__((format(printf, 4, 5)));

#define DOMINO_PRINT_MESSAGE(...) dominoPrintMessageAt(__FILE__, __LINE__, __func__, __VA_ARGS__)

/**
 * @brief 将当前 UTC 时间格式化为 `YYYY-MM-DDTHH:MM:SS.mmmZ` 写入缓冲区。
 * @note 年份限定为 0000～9999；各字段按固定宽度补零，成功时输出 24 个字符。
 * @param out_ptr 输出缓冲；非空且容量非零时，失败保持空字符串。
 * @param out_len 缓冲区长度（含结尾空字符），完整输出至少需要 25 字节。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；
 * 日期转换或字符串格式化失败返回 `ERR_STRING_FORMAT`；年份越界返回 `ERR_OUT_OF_RANGE`；缓冲区不足返回 `ERR_BUFFER_TOO_SMALL`。
 * @note 时钟采样失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
DOMINO_CODE dominoFormatUtcTimestampNow(char* out_ptr, size_t out_len);

/**
 * @brief 创建单级目录。
 *
 * 当前实现调用 POSIX `mkdir(path, 0755)`；不会递归创建父目录，权限受进程 umask 影响。
 * @note ISO C（含 C23）没有创建目录的标准库接口。
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
 * @brief 采样 64 位 UTC 墙上时间戳毫秒，供存档生命周期文件命名使用；失败不降级或重试。
 * @return 当前 UTC 墙上时间戳毫秒。
 * @note 采样失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 * @note 不受引擎冻结控制，不是累计未冻结现实毫秒，不能用于任务、game_timer 或收益结算的业务计时。
 */
uint64_t dominoWallTimeMs(void);

/**
 * @brief 采样 64 位单调时间纳秒，供时间模块初始化、冻结、解冻及累计时间计算使用；失败不降级或重试。
 * @return 当前单调时间纳秒。
 * @note 采样失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 * @note 只用于计算间隔，不可转换为 UTC 时间；业务计时须使用 time 模块的累计未冻结现实毫秒接口。
 */
uint64_t dominoMonotonicTimeNs(void);

/**
 * @brief 获取当前进程 ID，用于临时文件和目录命名。
 * @note 调用 POSIX `getpid()`；ISO C（含 C23）没有获取进程 ID 的标准库接口。
 */
uint64_t dominoGetPid(void);

/**
 * @brief 以二进制追加模式打开可选文件。
 * @param file_path 文件路径；nullptr 或空字符串表示未配置。
 * @return 未配置时返回 nullptr，否则返回由调用方负责关闭的文件流。
 * @note 打开失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
FILE* dominoOpenAppendFile(const char* file_path);

/**
 * @brief 刷新标准库缓冲并关闭输出文件。
 * @param file_ptr 调用方拥有的有效输出文件流；不得传入标准输出流或已关闭的文件流。
 * @note 依次调用 fflush() 和 fclose()，不调用 fsync()，不提供落盘同步保证。
 * @note 空指针、刷新或关闭失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
void dominoFlushAndCloseFile(FILE* file_ptr);

/**
 * @brief 将标准库 FILE* 的缓冲刷出并通过 `fsync(fileno(file_ptr))` 尽力落盘。
 * @note 先调用 ISO C 标准库 `fflush()`，成功后调用 POSIX `fileno()` 和 `fsync()`。
 * ISO C（含 C23）没有等价的存储同步接口；仅调用 `fflush()` 不提供落盘同步语义。
 * @return 0 成功；非 0 失败。
 */
int dominoFlushFile(FILE* file_ptr);

/**
 * @brief 递归删除文件或目录；路径不存在时视为成功，遇到符号链接时只删除链接本身。
 * @note 忽略非根路径的末尾 '/'，包括普通文件和符号链接路径；全部由 '/' 组成的根路径保持原样。
 * @return `CODE_OK` 成功；空指针/空路径分别返回 `ERR_NULL_POINTER`/`ERR_INVALID_PARAM`；路径查询失败返回 `ERR_FILE_STAT`；
 * 目录不可读返回 `ERR_DIRECTORY_OPEN`；删除失败返回 `ERR_FILE_REMOVE`；其余遍历失败统一返回 `ERR_DIRECTORY_READ`。
 * @note nftw 内部分配失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 * @note 使用 lstat() 区分类型：非目录（包括根符号链接）直接 remove()，真实目录才使用
 * POSIX `nftw(FTW_DEPTH | FTW_PHYS)` 后序遍历并调用 remove()；ISO C（含 C23）没有目录遍历接口。
 * 失败即停止，不回滚已完成的删除；遍历资源由 nftw 管理，返回的 IO 结果交由调用方按业务语义处理。
 */
DOMINO_CODE dominoRemovePathRecursive(const char* path);

/**
 * @brief 以二进制只读方式读入整个文件到堆缓冲区。
 * @param path 文件路径。
 * @param data_ptr 输出堆缓冲区；成功时由调用方 `free`，空文件时为 nullptr。
 * @param length_ptr 输出字节数；空文件时为 0。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；空路径返回 `ERR_INVALID_PARAM`；
 * 打开、读取或关闭失败分别返回 `ERR_FILE_OPEN`、`ERR_FILE_READ`、`ERR_FILE_CLOSE`。
 * @note 两个输出参数均须非空；均非空时，失败输出保持 nullptr 和 0。
 * @note 内存分配失败直接打印 stderr 并 exit(EXIT_FAILURE)；返回 IO 错误前关闭文件并释放缓冲，保留首先发生的错误。
 */
DOMINO_CODE dominoReadFileBytes(const char* path, uint8_t** data_ptr, size_t* length_ptr);

/**
 * @brief 以“同目录临时文件 + rename”的方式写入目标文件，避免出现半写入文件。
 * @param path 目标文件路径。
 * @param data 待写入内容；当前实现要求非 NULL。
 * @param len 待写入字节数，须大于 0。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；空路径或零长度返回 `ERR_INVALID_PARAM`；
 * 临时文件创建或流打开失败返回 `ERR_FILE_OPEN`；写入、同步、关闭或重命名失败分别返回
 * `ERR_FILE_WRITE`、`ERR_FILE_FLUSH`、`ERR_FILE_CLOSE`、`ERR_FILE_RENAME`。
 * @note 临时路径缓冲区大小计算溢出直接打印 stderr 并 exit(EXIT_FAILURE)。
 * @note 使用 POSIX `mkstemp()` 排他创建同目录临时文件，再通过 `fdopen()` 写入；不依赖 PID 或时钟生成文件名。
 * 临时文件以 0600 权限创建（受 umask 限制），重命名后目标文件沿用该权限；原子替换依赖 POSIX rename 语义。
 * 返回 IO 错误前关闭文件并尝试删除临时文件，保留首先发生的错误；删除失败可能留下临时文件，不重试。
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
 * @brief 封装 ISO C 标准库 `rename()`，将源路径重命名为目标路径。
 * @note 当前存档流程依赖 POSIX 在同一文件系统内原子替换已有目标文件的语义。
 * ISO C 对目标路径已存在时的行为定义为实现定义，不能仅凭标准 C 接口保证原子覆盖。
 * @return 0 成功；非 0 失败。
 */
int dominoRename(const char* tmp_path, const char* final_path);
#endif
