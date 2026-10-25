#ifndef DOMINO_ENGINE_REMOTE_SERVER_LOGGER_MODULE_H
#define DOMINO_ENGINE_REMOTE_SERVER_LOGGER_MODULE_H

#include "domino_shared_logger.h"

/**
 * @brief 准备本模块独立的日志资源，不启动 writer；重复初始化或其他致命错误直接终止。
 * @note 生命周期重复或错误顺序直接打印 stderr 并退出；支持 Init → Stop → Destroy。
 * @param config 日志文件、队列容量和默认级别；零值使用默认配置。
 */
void dominoEngineRemoteServerLoggerInit(DominoLogConfig config);

/** @brief 创建 writer；每次 Init 后至多启动一次，不支持 Stop 后重启。 */
void dominoEngineRemoteServerLoggerStart(void);

/** @brief 全部日志生产者结束后排空日志并 join writer、刷新输出；仅 Init 时跳过 join，保留资源。 */
void dominoEngineRemoteServerLoggerStop(void);

/** @brief 调用方先完成 Stop，再关闭文件并释放日志资源；不停止或 join writer，致命错误直接终止。 */
void dominoEngineRemoteServerLoggerDestroy(void);

/** @brief 在 Start 后、Stop 前通过日志队列记录运行阶段的格式化日志；非运行阶段的非致命诊断统一使用 DOMINO_PRINT_MESSAGE 直接写 stdout。 */
void dominoEngineRemoteServerLog(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) DOMINO_LOG_PRINTF_ATTR(3, 4);

#define DOMINO_ENGINE_REMOTE_SERVER_LOG(level, fmt, ...)                                                                  \
    dominoEngineRemoteServerLog((level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, \
                                (fmt)__VA_OPT__(, ) __VA_ARGS__)

#endif
