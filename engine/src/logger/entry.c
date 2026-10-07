#include "entry.h"

#include <stdarg.h>
#include <stddef.h>

static DominoLogger g_logger;
static DominoLogger* g_logger_ptr = nullptr;

DOMINO_CODE dominoLoggerModuleInit(void) {
    if (g_logger_ptr != nullptr) {
        return ERR_ALREADY_INITIALIZED;
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
        return (DOMINO_CODE)result;
    }
    g_logger_ptr = logger_ptr;
    return CODE_OK;
}

void dominoLoggerModuleExit(void) {
    dominoLogDestroy(g_logger_ptr);
    g_logger_ptr = nullptr;
}

void dominoEngineLog(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    dominoLogVa(g_logger_ptr, (domino_log_module_id_t)module, level, loc, fmt, arg_list);
    va_end(arg_list);
}

void dominoEngineLogMsg(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* msg_ptr) {
    dominoLogMsg(g_logger_ptr, (domino_log_module_id_t)module, level, loc, msg_ptr);
}
