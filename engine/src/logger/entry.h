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

/** @brief 写一条引擎运行期日志，异步输出。只允许在 logger Start 之后、Stop 之前调用。
 * @note ERROR 只是等级，不终止进程；致命错误用 DOMINO_EXIT_WITH_ERROR，生命周期诊断用 DOMINO_PRINT_MESSAGE（宪法 4.6）。
 */
void dominoEngineLog(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...)
    DOMINO_LOG_PRINTF_ATTR(4, 5);

#define DOMINO_ENGINE_LOG(module, level, fmt, ...)                                                                      \
    dominoEngineLog((module), (level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, \
                    (fmt)__VA_OPT__(, ) __VA_ARGS__)

/** @brief 初始化引擎日志实例并注册子模块，不启动 writer。 */
extern void dominoLoggerModuleInit(void);

/** @brief 创建 writer 线程。 */
extern void dominoLoggerModuleStart(void);

/** @brief 全部日志生产者结束后调用：排空日志、join writer 并同步输出，保留资源。 */
extern void dominoLoggerModuleStop(void);

/** @brief Stop 之后关闭文件并释放日志资源。 */
extern void dominoLoggerModuleDestroy(void);

#endif
