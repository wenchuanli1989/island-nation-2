#ifndef DOMINO_ENGINE_LOGGER_MODULE_H
#define DOMINO_ENGINE_LOGGER_MODULE_H

#include "domino_engine_log.h"
#include "domino_shared_error_codes.h"

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

void dominoEngineLog(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...)
    DOMINO_LOG_PRINTF_ATTR(4, 5);

#define DOMINO_ENGINE_LOG(module, level, fmt, ...)                                                                      \
    dominoEngineLog((module), (level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, \
                    (fmt)__VA_OPT__(, ) __VA_ARGS__)

void dominoEngineLogMsg(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* msg_ptr);

#define DOMINO_ENGINE_LOG_MSG(module, level, msg_ptr) \
    dominoEngineLogMsg((module), (level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, (msg_ptr))

/** @brief 创建 engine 独立日志实例并注册子模块；重复初始化返回 ERR_ALREADY_INITIALIZED。 */
extern DOMINO_CODE dominoLoggerModuleInit(void);

/** @brief 停止 engine 日志实例并排空队列；调用前须停止所有 engine 日志生产者。 */
extern void dominoLoggerModuleExit(void);

#endif
