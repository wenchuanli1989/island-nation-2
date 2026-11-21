#ifndef DOMINO_SHARED_LOGGER_H
#define DOMINO_SHARED_LOGGER_H

#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_lifecycle.h"

/** @file domino_shared_logger.h
 * @brief 异步日志：多个生产者整理记录后入队，每个已启动实例由一个 writer 线程按入队顺序写出（宪法第四章）。
 * @note 每条日志只写一个目标：模块文件优先，其次实例默认文件，都未配置时写 stdout；从不写 stderr。
 * 队列容量固定，满载时生产者先向同一输出流提示一次，再等待空位，不丢弃记录。
 * logger 自身的配置、分配、同步、格式化和文件操作失败均致命退出。
 */

typedef enum {
    DOMINO_LOG_LEVEL_TRACE = 1,
    DOMINO_LOG_LEVEL_DEBUG = 2,
    DOMINO_LOG_LEVEL_INFO = 3,
    DOMINO_LOG_LEVEL_WARN = 4,
    DOMINO_LOG_LEVEL_ERROR = 5,
} DOMINO_LOG_LEVEL;

/** @brief 日志来源位置；写日志函数在返回前复制并截断字符串，writer 不借用调用方内存。 */
typedef struct DominoLogSourceLocation {
    const char* file;  ///< 通常为 `__FILE__`，可为空。
    int line;          ///< 小于等于 0 时按 0 输出。
    const char* func;  ///< 通常为 `__func__`，可为空。
} DominoLogSourceLocation;

typedef struct DominoLogConfig {
    const char* file_path;       ///< 实例默认日志文件；空表示未配置。
    size_t queue_capacity;       ///< 队列容量（条数），0 使用默认值，不得超过 UINT32_MAX；运行中不扩容。
    DOMINO_LOG_LEVEL min_level;  ///< 模块未指定等级时的默认最小等级，0 表示 INFO。
} DominoLogConfig;

#define DOMINO_LOG_PRINTF_ATTR(m, n) __attribute__((format(printf, m, n)))

typedef uint8_t domino_log_module_id_t;

#define DOMINO_LOG_MODULE_COUNT_MAX 128U
#define DOMINO_LOG_MODULE_NAME_MAX 16U

/** @brief 日志模块配置；module_id 和 name 在同一实例内唯一，name 须在数组内以 NUL 结尾。 */
typedef struct {
    char name[DOMINO_LOG_MODULE_NAME_MAX];
    const char* file_path;       ///< 模块专用文件，空时继承实例默认输出；仅用于特殊调试，须避免与其他输出流指向同一文件。
    FILE* output_ptr;            ///< logger 解析出的实际输出流，Init 忽略传入值。
    DOMINO_LOG_LEVEL min_level;  ///< 0 表示继承实例默认等级。
    domino_log_module_id_t module_id;
} DominoLogModule;

static_assert(sizeof(DominoLogModule) <= 64, "DominoLogModule must be less than 64 bytes");

struct DominoThreadQueue;

/** @brief 日志实例；存储由调用方提供，成员只由 logger 维护。
 * @note 首次对象须零初始化；Init 后不得复制或移动，存储保持有效直到 Destroy 返回。
 */
typedef struct DominoLogger {
    DominoLogModule module_list[DOMINO_LOG_MODULE_COUNT_MAX];
    thrd_t writer_thread;
    FILE* default_file_ptr;  ///< 实例拥有的默认日志文件，未配置时为空。
    struct DominoThreadQueue* queue_ptr;
    size_t module_count;
    _Atomic uint32_t last_seq;  ///< 最后分配的日志序号。
    DOMINO_MODULE_LIFECYCLE_STATE lifecycle_state;
} DominoLogger;

/** @brief 校验配置，准备固定容量队列、模块表和文件，不创建 writer。
 * @param modules_ptr 模块配置，仅在本次调用中读取；module_count 为 0 时可为空。
 * @pre logger_ptr 为零初始化或已 Destroy 的对象。
 * @note 默认等级在此解析到各模块，实例不另存。Init 后模块表只读。
 */
void dominoLogInit(DominoLogConfig config, const DominoLogModule* modules_ptr, size_t module_count, DominoLogger* logger_ptr);

/** @brief 创建 writer 线程，此后才允许写日志。每次 Init 后至多调用一次。 */
void dominoLogStart(DominoLogger* logger_ptr);

/** @brief 关闭入队（BeginFlush），等 writer 写完已收日志并 join，再对各文件 fflush + fsync、对 stdout fflush；保留队列和文件。
 * @pre 本实例的全部日志调用已结束且不再有新调用；不得与该实例其他接口并发。
 * @note 仅 Init 未 Start 时不 join，但同样关闭入队并同步输出。继承的默认文件只同步一次。Stop 后只能 Destroy。
 */
void dominoLogStop(DominoLogger* logger_ptr);

/** @brief 关闭文件（继承的默认文件只关一次，不关 stdout）、释放队列并清零对象，不 join writer。
 * @pre 已 Stop。
 */
void dominoLogDestroy(DominoLogger* logger_ptr);

/** @brief 格式化并异步记录一条日志，支持多个生产者并发。
 * @note 空实例、空格式串、非法等级、未注册或被过滤的模块直接丢弃。通过过滤后，调用线程采样 UTC 时间、复制来源与正文并入队；
 * 正文为空不入队。只允许在 Start 之后、Stop 之前调用，否则致命退出。
 */
void dominoLogVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt,
                 va_list arg_list) DOMINO_LOG_PRINTF_ATTR(5, 0);

#endif
