#include "entry.h"

#include <stdarg.h>
#include <stddef.h>

#include "error.h"

static DominoLogger g_logger;
static DominoLogger* g_logger_ptr = nullptr;
static DOMINO_CODE g_lifecycle_result = CODE_OK;

#define DOMINO_ENGINE_ERROR_BODY_MAX 512U
#define DOMINO_ENGINE_ERROR_FILE_MAX 80U
#define DOMINO_ENGINE_ERROR_FUNCTION_MAX 32U

static const char* dominoEngineLogModuleName(DOMINO_ENGINE_LOG_MODULE module) {
    switch (module) {
        case DOMINO_ENGINE_LOG_MODULE_CORE:
            return "core";
        case DOMINO_ENGINE_LOG_MODULE_STORAGE:
            return "storage";
        case DOMINO_ENGINE_LOG_MODULE_COMMON:
            return "common";
        case DOMINO_ENGINE_LOG_MODULE_HOST:
            return "host";
        case DOMINO_ENGINE_LOG_MODULE_HUMAN:
            return "human";
        case DOMINO_ENGINE_LOG_MODULE_NAV:
            return "nav";
        case DOMINO_ENGINE_LOG_MODULE_SOCIAL:
            return "social";
    }
    return nullptr;
}

static bool dominoEngineLogSourceIsTruncated(const char* source_str, size_t max_len) {
    for (size_t i = 0U; i < max_len; i++) {
        if (source_str[i] == '\0') {
            return false;
        }
    }
    return source_str[max_len] != '\0';
}

/** @brief 错误诊断同步输出，日志故障不能抢先上报并覆盖调用方即将传播的业务首错。 */
static void dominoEngineLogErrorVa(DOMINO_ENGINE_LOG_MODULE module, DominoLogSourceLocation loc, const char* fmt, va_list arg_list)
    DOMINO_LOG_PRINTF_ATTR(3, 0);

static void dominoEngineLogErrorVa(DOMINO_ENGINE_LOG_MODULE module, DominoLogSourceLocation loc, const char* fmt, va_list arg_list) {
    const char* module_name = dominoEngineLogModuleName(module);
    if (module_name == nullptr || fmt == nullptr) {
        return;
    }

    const char* file_str = loc.file != nullptr ? loc.file : "?";
    const char* function_str = loc.func != nullptr ? loc.func : "?";
    const int line = loc.line > 0 ? loc.line : 0;
    const bool source_truncated = dominoEngineLogSourceIsTruncated(file_str, DOMINO_ENGINE_ERROR_FILE_MAX) ||
                                  dominoEngineLogSourceIsTruncated(function_str, DOMINO_ENGINE_ERROR_FUNCTION_MAX);
    char body_buf[DOMINO_ENGINE_ERROR_BODY_MAX];
    int formatted_len = vsnprintf(body_buf, sizeof(body_buf), fmt, arg_list);
    if (formatted_len < 0) {
        (void)fprintf(stderr, "[ERROR][%s][%.*s:%d:%.*s] error log formatting failed (%s, code=%d)%s\n", module_name,
                      (int)DOMINO_ENGINE_ERROR_FILE_MAX, file_str, line, (int)DOMINO_ENGINE_ERROR_FUNCTION_MAX, function_str,
                      dominoErrorCodeToString(ERR_STRING_FORMAT), (int)ERR_STRING_FORMAT, source_truncated ? "...(TRUNCATED)" : "");
        return;
    }
    if (formatted_len == 0) {
        return;
    }

    const bool truncated = (size_t)formatted_len >= sizeof(body_buf) || source_truncated;
    (void)fprintf(stderr, "[ERROR][%s][%.*s:%d:%.*s] %s%s\n", module_name, (int)DOMINO_ENGINE_ERROR_FILE_MAX, file_str, line,
                  (int)DOMINO_ENGINE_ERROR_FUNCTION_MAX, function_str, body_buf, truncated ? "...(TRUNCATED)" : "");
}

DOMINO_CODE dominoLoggerModuleInit(void) {
    if (g_lifecycle_result != CODE_OK) {
        return g_lifecycle_result;
    }
    if (g_logger_ptr != nullptr) {
        g_lifecycle_result = DOMINO_LOG_DIRECT_ERROR(ERR_ALREADY_INITIALIZED, "engine logger is already initialized");
        return g_lifecycle_result;
    }

    static const DominoLogModule modules[] = {
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_CORE, .name = "core"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_STORAGE, .name = "storage"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_COMMON, .name = "common"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_HOST, .name = "host"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_HUMAN, .name = "human"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_NAV, .name = "nav"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_SOCIAL, .name = "social"},
    };
    DominoLogger* logger_ptr = &g_logger;
    int result = dominoLogInit((DominoLogConfig){0}, modules, sizeof(modules) / sizeof(modules[0]), logger_ptr);
    if (result != CODE_OK) {
        g_lifecycle_result = (DOMINO_CODE)result;
        return g_lifecycle_result;
    }
    g_logger_ptr = logger_ptr;
    return CODE_OK;
}

DOMINO_CODE dominoLoggerModuleExit(void) {
    if (g_lifecycle_result != CODE_OK) {
        return g_lifecycle_result;
    }
    int result = dominoLogDestroy(g_logger_ptr);
    if (result != CODE_OK) {
        g_lifecycle_result = (DOMINO_CODE)result;
        return g_lifecycle_result;
    }
    g_logger_ptr = nullptr;
    return CODE_OK;
}

void dominoEngineLog(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    if (level == DOMINO_LOG_LEVEL_ERROR) {
        dominoEngineLogErrorVa(module, loc, fmt, arg_list);
    } else {
        dominoLogVa(g_logger_ptr, (domino_log_module_id_t)module, level, loc, fmt, arg_list);
    }
    va_end(arg_list);
}

void dominoEngineLogMsg(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* msg_ptr) {
    if (level == DOMINO_LOG_LEVEL_ERROR) {
        if (msg_ptr != nullptr) {
            dominoEngineLog(module, level, loc, "%s", msg_ptr);
        }
        return;
    }
    dominoLogMsg(g_logger_ptr, (domino_log_module_id_t)module, level, loc, msg_ptr);
}
