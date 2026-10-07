#ifndef DOMINO_ENGINE_REMOTE_LOCAL_LOGGER_MODULE_H
#define DOMINO_ENGINE_REMOTE_LOCAL_LOGGER_MODULE_H

#include "domino_engine_log.h"
#include "domino_shared_error_codes.h"

/**
 * @brief 初始化本模块独立的日志实例。
 * @param config 日志文件、队列容量及默认级别；零值使用默认配置。
 * @return 成功返回 CODE_OK，失败返回负值；重复初始化返回 ERR_ALREADY_INITIALIZED。
 */
DOMINO_CODE dominoEngineRemoteLocalLoggerInit(DominoLogConfig config);

/** @brief 停止本模块日志实例并排空队列；调用前须停止所有日志生产者。 */
void dominoEngineRemoteLocalLoggerExit(void);

/** @brief 向本模块日志实例写入格式化消息。 */
void dominoEngineRemoteLocalLog(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) DOMINO_LOG_PRINTF_ATTR(3, 4);

#define DOMINO_ENGINE_REMOTE_LOCAL_LOG(level, fmt, ...)                                                                  \
    dominoEngineRemoteLocalLog((level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, \
                               (fmt)__VA_OPT__(, ) __VA_ARGS__)

/** @brief 向本模块日志实例写入无需格式化的消息。 */
void dominoEngineRemoteLocalLogMsg(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* msg_ptr);

#define DOMINO_ENGINE_REMOTE_LOCAL_LOG_MSG(level, msg_ptr) \
    dominoEngineRemoteLocalLogMsg((level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, (msg_ptr))

#endif
