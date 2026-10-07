#ifndef DOMINO_ENGINE_LOG_H
#define DOMINO_ENGINE_LOG_H

#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>

typedef enum {
    DOMINO_LOG_LEVEL_TRACE = 1,
    DOMINO_LOG_LEVEL_DEBUG = 2,
    DOMINO_LOG_LEVEL_INFO = 3,
    DOMINO_LOG_LEVEL_WARN = 4,
    DOMINO_LOG_LEVEL_ERROR = 5,
} DOMINO_LOG_LEVEL;

/**
 * @brief 日志来源位置。
 *
 * 写日志函数会在返回前复制并截断 `file`、`func`，异步 writer 不借用调用方字符串。
 */
typedef struct DominoLogSourceLocation {
    const char* file;  ///< 通常为 `__FILE__`；必须为 NUL 结尾字符串或 nullptr。
    int line;          ///< 通常为 `__LINE__`，小于等于 0 时按 0 输出。
    const char* func;  ///< 通常为 `__func__`；必须为 NUL 结尾字符串或 nullptr。
} DominoLogSourceLocation;

typedef struct DominoLogConfig {
    /** @brief 可选日志文件路径；为空时只写 stdout/stderr。 */
    const char* file_path;

    /**
     * @brief 本实例的日志队列容量（元素个数）。
     *
     * 为 0 时使用默认容量，非零值不得超过 UINT32_MAX；队列固定容量，满载时阻塞等待空位。
     * 每个队列元素的正文缓冲当前固定为 256 字节，最终输出行也使用同尺寸缓冲，
     * 因此前缀过长或正文过长时会追加截断标记。
     */
    size_t buffer_size;

    /**
     * @brief 模块未指定 min_level 时使用的默认最小日志等级，不限制模块显式设置的等级。
     *
     * 为 0 时使用 `INFO`，其他值必须在 `TRACE..ERROR` 范围内。
     */
    DOMINO_LOG_LEVEL min_level;
} DominoLogConfig;

#define DOMINO_LOG_PRINTF_ATTR(m, n) __attribute__((format(printf, m, n)))

typedef uint8_t domino_log_module_id_t;

/**
 * @brief 日志模块初始化配置。
 *
 * `module_id` 和 `name` 在同一实例内必须唯一，不同实例可以复用；`name` 必须在固定数组内以 NUL 结尾。
 */
#define DOMINO_LOG_MAX_MODULES 128U
#define DOMINO_LOG_MODULE_NAME_MAX 16U

typedef struct {
    domino_log_module_id_t module_id;
    /** @brief 为 0 时继承实例默认等级；显式设置 `TRACE..ERROR` 时独立生效。 */
    DOMINO_LOG_LEVEL min_level;
    char name[DOMINO_LOG_MODULE_NAME_MAX];
} DominoLogModule;

static_assert(sizeof(DominoLogModule) <= 64, "DominoLogModule must be less than 64 bytes");

struct DominoThreadQueue;

/**
 * @brief 独立日志服务；调用方提供实例存储，内部资源由 engine 管理。
 *
 * 成员仅供 engine 内部使用，调用方不得直接修改。初始化成功后不得复制或移动对象，
 * 必须保持对象地址和存储有效，直到 dominoLogDestroy 返回。
 */
typedef struct DominoLogger {
    DominoLogModule modules[DOMINO_LOG_MAX_MODULES];
    thrd_t writer_thread;
    FILE* file_ptr;
    struct DominoThreadQueue* queue_ptr;
    size_t module_count;
    _Atomic uint32_t seq;
    DOMINO_LOG_LEVEL default_min_level;
} DominoLogger;

/**
 * @brief 在调用方提供的对象上初始化日志服务，每个实例拥有独立队列、写线程、模块表、文件和序号。
 *
 * writer 异步写 stdout/stderr 以及可选文件；生产者在本实例队列满时阻塞等待空位。
 * 多个实例可同时存在并独立初始化、销毁；stdout/stderr 是共享输出流，实例间不保证输出顺序。
 * 需要隔离文件输出时，应为各实例指定不同的 file_path。
 * 模块列表在分配内部资源和启动 writer 前完成校验并复制，初始化成功后模块表只读，直至销毁实例。
 * 对象无需预先清零，但不得对尚未销毁的已初始化对象再次调用。失败时释放已获取的内部资源并清空对象，
 * 可直接重试初始化；销毁后也可在同一对象上重新初始化。
 *
 * @param config 实例配置；file_path 仅在本次调用中使用，不保留调用方指针。
 * @param modules_ptr 模块配置数组，仅在本次调用中使用；module_count 为 0 时可为 nullptr。
 * @param module_count 模块数量，不得超过 DOMINO_LOG_MAX_MODULES；为 0 时初始化空模块表。
 * @param logger_ptr 调用方提供的 DominoLogger 对象地址，存储在销毁完成前必须保持有效。
 * @return 0 成功；空对象或非空列表的空指针返回 ERR_NULL_POINTER；非法配置返回 ERR_INVALID_PARAM；
 *         模块数量或队列容量超限返回 ERR_OUT_OF_RANGE；容量乘法溢出返回 ERR_MEMORY_OVERFLOW；
 *         重复 ID 或名称返回 ERR_ALREADY_EXISTS；其他失败返回具体负错误码。
 */
int dominoLogInit(DominoLogConfig config, const DominoLogModule* modules_ptr, size_t module_count, DominoLogger* logger_ptr);

/**
 * @brief 排空指定实例的队列、刷新输出并退出写线程，释放内部资源（阻塞）。
 *
 * 调用前必须停止并等待该实例的所有 producer，且不得与该实例的其他 API 调用并发。
 * 其他实例仍可正常使用。不释放调用方提供的对象存储，返回后对象清空，可重新初始化或由调用方释放存储。
 * nullptr、零初始化对象、初始化失败后的对象和已销毁对象均为无操作；不得传入内容未初始化的对象。
 */
void dominoLogDestroy(DominoLogger* logger_ptr);

/**
 * @brief 格式化并异步记录日志到指定实例；同一实例支持多个 producer 并发调用。
 *
 * 空实例、非法日志等级或未注册模块会被丢弃；source location 会在函数返回前复制到队列元素。
 */
void dominoLogVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt,
                 va_list arg_list) DOMINO_LOG_PRINTF_ATTR(5, 0);

/** @brief 格式化日志的可变参数入口，生命周期与过滤规则同 dominoLogVa。 */
void dominoLog(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...)
    DOMINO_LOG_PRINTF_ATTR(5, 6);

/** @brief 记录已格式化正文，写线程负责添加前缀和换行；空实例为无操作。 */
void dominoLogMsg(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc,
                  const char* msg_ptr);

/**
 * @brief 按长度记录已格式化正文；空实例为无操作。
 *
 * 正文按 msg_len 字节复制，内部 NUL 字节会作为正文内容写出；超过内部缓冲时截断。
 */
void dominoLogMsgN(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc,
                   const char* msg_ptr, size_t msg_len);

#endif
