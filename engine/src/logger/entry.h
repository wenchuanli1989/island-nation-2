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

/**
 * @brief ERROR 同步写入 stderr；其他等级交给 engine 日志实例异步处理。
 * @note ERROR 不依赖日志实例初始化，不入队、不上报日志故障，避免覆盖调用方随后传播的原始错误。
 *       使用有界栈缓冲，正文或来源位置超限标记 TRUNCATED；输出失败不重试或递归记录。
 */
void dominoEngineLog(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...)
    DOMINO_LOG_PRINTF_ATTR(4, 5);

#define DOMINO_ENGINE_LOG(module, level, fmt, ...)                                                                      \
    dominoEngineLog((module), (level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, \
                    (fmt)__VA_OPT__(, ) __VA_ARGS__)

/** @brief 已格式化正文入口，ERROR 同步输出规则与 dominoEngineLog 一致。 */
void dominoEngineLogMsg(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* msg_ptr);

#define DOMINO_ENGINE_LOG_MSG(module, level, msg_ptr) \
    dominoEngineLogMsg((module), (level), (DominoLogSourceLocation){.file = __FILE__, .line = __LINE__, .func = __func__}, (msg_ptr))

/**
 * @brief 创建 engine 独立日志实例并注册子模块。
 * @return 成功返回 CODE_OK；重复初始化返回并保存 ERR_ALREADY_INITIALIZED，其他失败返回负值。
 *         初始化或退出出现首错后，后续 Init/Exit 仅返回该错误，不重试。
 * @note 生命周期失败后保活已有资源，由宿主决定何时结束进程。
 */
extern DOMINO_CODE dominoLoggerModuleInit(void);

/** @brief 停止 engine 日志实例并排空队列；调用前须停止所有 engine 日志生产者，失败立即返回并保留剩余资源。 */
extern DOMINO_CODE dominoLoggerModuleExit(void);

#endif
