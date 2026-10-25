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
    /** @brief 可选默认日志文件路径；nullptr 或空字符串表示未配置，模块也未配置文件时写 stdout。 */
    const char* file_path;

    /**
     * @brief 本实例的日志队列容量（元素个数）。
     *
     * 为 0 时使用默认容量，非零值不得超过 UINT32_MAX；队列固定容量，满载时向当前模块的输出流提示并等待直到入队。
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
 * @brief 日志模块配置及实例内的运行数据。
 *
 * `module_id` 和 `name` 在同一实例内必须唯一，不同实例可以复用；`name` 必须在固定数组内以 NUL 结尾。
 */
#define DOMINO_LOG_MAX_MODULES 128U
#define DOMINO_LOG_MODULE_NAME_MAX 16U

typedef struct {
    char name[DOMINO_LOG_MODULE_NAME_MAX];
    /** @brief 可选模块日志文件路径；nullptr 或空字符串时继承实例默认文件，两者都未配置时写 stdout。 */
    const char* file_path;
    /** @brief logger 维护的实际输出流；调用方无需设置，初始化时忽略传入值。 */
    FILE* output_ptr;
    /** @brief 为 0 时继承实例默认等级；显式设置 `TRACE..ERROR` 时独立生效。 */
    DOMINO_LOG_LEVEL min_level;
    domino_log_module_id_t module_id;
} DominoLogModule;

static_assert(sizeof(DominoLogModule) <= 64, "DominoLogModule must be less than 64 bytes");

struct DominoThreadQueue;

/**
 * @brief 独立日志服务；调用方提供实例存储，内部资源由 shared 日志实现管理。
 *
 * 成员仅供日志实现使用，调用方不得直接修改。初始化成功后不得复制或移动对象，
 * 必须保持对象地址和存储有效，直到 dominoLogDestroy 返回；致命错误直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
typedef struct DominoLogger {
    DominoLogModule modules[DOMINO_LOG_MAX_MODULES];
    thrd_t writer_thread;
    FILE* file_ptr;  ///< 实例拥有的默认日志文件；未配置时为 nullptr。
    struct DominoThreadQueue* queue_ptr;
    size_t module_count;
    _Atomic uint32_t seq;
    DominoModuleLifecycleState lifecycle_state;
} DominoLogger;

/**
 * @brief 在调用方提供的对象上初始化日志资源，每个实例拥有独立队列、模块表、文件和序号，不启动写线程。
 *
 * 各等级日志只写一个目标，优先模块文件，其次实例默认文件，两者都未配置时写 stdout；日志内容不写 stderr。
 * 满队列时生产者向当前模块的同一输出流同步提示一次并刷新，再通过条件变量等待空位并入队。
 * 文件由实例以追加模式打开并负责关闭；stdout 只刷新、不关闭。多个实例可同时存在并独立初始化、销毁。
 * stdout 是共享输出流，实例间不保证输出顺序；需要隔离文件输出时，应为各实例和模块指定不同的 file_path。
 * 模块列表先完成校验，再分配队列、打开文件并解析模块状态；初始化成功后模块表只读，直至销毁实例。
 * 实例内模块的 file_path 保持 nullptr，output_ptr 由 logger 解析为模块文件、实例默认文件或 stdout。
 * 首次使用的对象须零初始化（静态存储、={0} 或 calloc）；Init 基于空对象创建资源，不重复整体清零，Destroy 返回后可直接再次 Init。
 * 默认日志等级仅在 Init 时解析到各模块，实例不另存一份默认等级。
 * Init 只接受未初始化状态，重复初始化或配置、容量、分配、文件、同步初始化失败时，
 * 直接打印 stderr 并 exit(EXIT_FAILURE)，不回滚或重试。正常销毁后可在同一对象上重新初始化。
 *
 * @param config 实例配置；file_path 仅在本次调用中使用。
 * @param modules_ptr 模块配置数组及其 file_path 仅在本次调用中使用；module_count 为 0 时可为 nullptr。
 * @param module_count 模块数量，不得超过 DOMINO_LOG_MAX_MODULES；为 0 时初始化空模块表。
 * @param logger_ptr 零初始化或已销毁的 DominoLogger 对象地址，存储在销毁完成前必须保持有效。
 */
void dominoLogInit(DominoLogConfig config, const DominoLogModule* modules_ptr, size_t module_count, DominoLogger* logger_ptr);

/**
 * @brief 启动已初始化实例的写线程；成功后才允许记录日志。
 * @pre 每次 Init 后至多调用一次，且尚未 Stop；生命周期由调用方串行管理。
 * @note 未初始化、重复启动、停止后重启或线程创建失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
void dominoLogStart(DominoLogger* logger_ptr);

/**
 * @brief 拒绝新消息，排空日志队列并 join 写线程，刷新输出流（阻塞），保留全部资源。
 *
 * 调用前必须停止并等待该实例的所有 producer，且不得与该实例的其他 API 调用并发。
 * 通过 dominoThreadQueueBeginFlush() 拒绝新消息并唤醒 writer，writer 排空后退出，调用方 join 等待完成。
 * writer 退出后刷新各模块文件、实例默认文件及被使用的 stdout，不关闭文件或释放队列；其他实例仍可正常使用。
 * 仅 Init、未 Start 时仍关闭入队并刷新输出，但不 join 未创建的线程。
 * 每次 Init 后只调用一次 Stop，返回后不得继续写日志或直接重新初始化，须由调用方随后调用 Destroy。
 * 仅接受 INITIALIZED 或 STARTED；空指针、未初始化、重复 Stop、队列同步、join 或刷新文件失败时，
 * 直接打印 stderr 并 exit(EXIT_FAILURE)。不得传入内容未初始化的对象。
 */
void dominoLogStop(DominoLogger* logger_ptr);

/**
 * @brief 关闭指定实例的文件并释放队列等内部资源，不停止或 join 写线程。
 *
 * 已初始化的实例必须先完成 Stop；调用方串行管理 Stop → Destroy，不得与该实例的其他 API 调用并发。
 * 各模块文件分别关闭，继承的默认文件只关闭一次；stdout 不关闭，其他实例仍可正常使用。
 * 不释放调用方提供的对象存储，返回后对象清空，可重新初始化或释放存储。
 * 仅接受 STOPPED；空指针、未初始化、未 Stop、重复 Destroy、队列销毁、刷新或关闭文件失败时，
 * 直接打印 stderr 并 exit(EXIT_FAILURE)。不得传入内容未初始化的对象。
 */
void dominoLogDestroy(DominoLogger* logger_ptr);

/**
 * @brief 格式化并异步记录日志到指定实例；同一实例支持多个 producer 并发调用。
 *
 * 空实例、空格式串指针、非法日志等级、未注册或被等级过滤的模块会被丢弃；source location 会在函数返回前复制到队列元素。
 * 公共入口先过滤输入，内部模块查找与格式化辅助函数使用已验证的值，不重复校验实例或等级。
 * 队列满时向当前模块的输出流同步提示一次并刷新，再等待直到本条日志入队；等待期间 writer 继续消费，不丢弃日志。
 * 已初始化实例只允许在 Start 后、Stop 前写日志；未 Start 或 Stop 后写入属于致命调用错误，不入队或等待。
 * 格式化、其他入队错误或写出失败直接打印 stderr 并 exit(EXIT_FAILURE)，诊断不进入异步日志队列。
 */
void dominoLogVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt,
                 va_list arg_list) DOMINO_LOG_PRINTF_ATTR(5, 0);

#endif
