#include "entry.h"

#include <stdarg.h>

static DominoLogger g_logger;
static DominoLogger* g_logger_ptr = nullptr;
static DOMINO_CODE g_lifecycle_result = CODE_OK;

DOMINO_CODE dominoEngineRemoteServerLoggerInit(DominoLogConfig config) {
    if (g_lifecycle_result != CODE_OK) {
        return g_lifecycle_result;
    }
    if (g_logger_ptr != nullptr) {
        g_lifecycle_result = ERR_ALREADY_INITIALIZED;
        return g_lifecycle_result;
    }

    static const DominoLogModule modules[] = {
        {.module_id = 0U, .name = "remote-server"},
    };
    DominoLogger* logger_ptr = &g_logger;
    int result = dominoLogInit(config, modules, sizeof(modules) / sizeof(modules[0]), logger_ptr);
    if (result != CODE_OK) {
        g_lifecycle_result = (DOMINO_CODE)result;
        return g_lifecycle_result;
    }
    g_logger_ptr = logger_ptr;
    return CODE_OK;
}

DOMINO_CODE dominoEngineRemoteServerLoggerExit(void) {
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

void dominoEngineRemoteServerLog(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    dominoLogVa(g_logger_ptr, 0U, level, loc, fmt, arg_list);
    va_end(arg_list);
}

void dominoEngineRemoteServerLogMsg(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* msg_ptr) {
    dominoLogMsg(g_logger_ptr, 0U, level, loc, msg_ptr);
}
