#ifndef DOMINO_ENGINE_LOGGER_MODULE_H
#define DOMINO_ENGINE_LOGGER_MODULE_H

#include "domino_shared_error_codes.h"
#include "domino_shared_logger.h"

typedef enum DOMINO_ENGINE_LOG_MODULE {
    DOMINO_ENGINE_LOG_MODULE_CORE = 0,
    DOMINO_ENGINE_LOG_MODULE_STORAGE = 1,
    DOMINO_ENGINE_LOG_MODULE_COMMON = 2,
    DOMINO_ENGINE_LOG_MODULE_HOST = 3,
    DOMINO_ENGINE_LOG_MODULE_HUMAN = 4,
    DOMINO_ENGINE_LOG_MODULE_NAV = 5,
    DOMINO_ENGINE_LOG_MODULE_SOCIAL = 6,
} DOMINO_ENGINE_LOG_MODULE;

#define DOMINO_ENGINE_LOG_MODULE_COUNT 7

/**
 * @brief 运行阶段所有等级交给日志实例异步处理。
 * @note ERROR 只表示日志等级，不终止进程；致命错误直接使用 DOMINO_EXIT_WITH_ERROR。
 *       初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断在调用点使用 DOMINO_PRINT_MESSAGE 直接写 stdout。
 *       本接口不判断或切换输出阶段。
 *       调用前须初始化并启动日志实例，只允许在 logger Start 后、Stop 前写入；所有生产者结束后才能 Stop。
 */
void dominoEngineLog(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...)
    DOMINO_LOG_PRINTF_ATTR(4, 5);

#define DOMINO_ENGINE_LOG(module, level, fmt, ...)                                                                      \
    dominoEngineLog((module), (level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, \
                    (fmt)__VA_OPT__(, ) __VA_ARGS__)

/** @brief 准备 engine 独立日志资源并注册子模块，不启动 writer。
 * @note 调用顺序由引擎保证，包装层不重复校验；shared logger 保留公共边界校验，支持 Init → Stop → Destroy。
 */
extern void dominoLoggerModuleInit(void);

/** @brief 在引擎启动阶段创建 writer；每次 Init 后至多启动一次，不支持 Stop 后重启。 */
extern void dominoLoggerModuleStart(void);

/** @brief 全部日志生产者结束后排空日志并 join writer、刷新输出；仅 Init 时跳过 join，保留资源。 */
extern void dominoLoggerModuleStop(void);

/** @brief 引擎先完成 Stop，再调用本接口关闭文件并释放日志资源；不停止或 join writer，致命错误直接终止。 */
extern void dominoLoggerModuleDestroy(void);

#endif
