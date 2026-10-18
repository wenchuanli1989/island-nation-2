#ifndef DOMINO_ENGINE_LOGGER_ERROR_H
#define DOMINO_ENGINE_LOGGER_ERROR_H

#include <stdio.h>

#include "domino_shared_error_codes.h"

/** @brief 队列和日志自身的故障直接写入 stderr，禁止重新进入异步日志队列。 */
static inline DOMINO_CODE dominoLogDirectErrorAt(DOMINO_CODE code, const char* reason_str, const char* file_str, int line, const char* function_str) {
    (void)fprintf(stderr, "[ERROR][%s:%d:%s] %s (%s, code=%d)\n", file_str, line, function_str, reason_str, dominoErrorCodeToString(code), (int)code);
    return code;
}

#define DOMINO_LOG_DIRECT_ERROR(code, reason_str) dominoLogDirectErrorAt((code), (reason_str), __FILE__, __LINE__, __func__)

#endif
