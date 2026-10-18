#ifndef DOMINO_CLIENT_HUB_LOGGER_MODULE_H
#define DOMINO_CLIENT_HUB_LOGGER_MODULE_H

#include "domino_engine_log.h"
#include "domino_shared_error_codes.h"

/**
 * @brief 初始化本模块独立的日志实例。
 * @param config 日志文件、队列容量及默认级别；零值使用默认配置。
 * @return 成功返回 CODE_OK；重复初始化返回并保存 ERR_ALREADY_INITIALIZED，其他失败返回负值。
 *         初始化或退出出现首错后，后续 Init/Exit 仅返回该错误，不重试。
 * @note 生命周期失败后保活已有资源，由宿主决定何时结束进程。
 */
DOMINO_CODE dominoClientHubLoggerInit(DominoLogConfig config);

/**
 * @brief 停止本模块日志实例并排空队列；调用前须停止所有日志生产者。
 * @return 成功返回 CODE_OK；失败返回负值并保留实例，后续 Init/Exit 返回首次失败，不重试。
 */
DOMINO_CODE dominoClientHubLoggerExit(void);

/** @brief 向本模块日志实例写入格式化消息。 */
void dominoClientHubLog(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) DOMINO_LOG_PRINTF_ATTR(3, 4);

#define DOMINO_CLIENT_HUB_LOG(level, fmt, ...) \
    dominoClientHubLog((level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, (fmt)__VA_OPT__(, ) __VA_ARGS__)

/** @brief 向本模块日志实例写入无需格式化的消息。 */
void dominoClientHubLogMsg(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* msg_ptr);

#define DOMINO_CLIENT_HUB_LOG_MSG(level, msg_ptr) \
    dominoClientHubLogMsg((level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, (msg_ptr))

#endif
