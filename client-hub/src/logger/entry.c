#include "entry.h"

#include <stdarg.h>

static DominoLogger g_logger;
static DominoLogger* g_logger_ptr = nullptr;

DOMINO_CODE dominoClientHubLoggerInit(DominoLogConfig config) {
    if (g_logger_ptr != nullptr) {
        return ERR_ALREADY_INITIALIZED;
    }

    static const DominoLogModule modules[] = {
        {.module_id = 0U, .name = "client-hub"},
    };
    DominoLogger* logger_ptr = &g_logger;
    int result = dominoLogInit(config, modules, sizeof(modules) / sizeof(modules[0]), logger_ptr);
    if (result != CODE_OK) {
        return (DOMINO_CODE)result;
    }
    g_logger_ptr = logger_ptr;
    return CODE_OK;
}

void dominoClientHubLoggerExit(void) {
    dominoLogDestroy(g_logger_ptr);
    g_logger_ptr = nullptr;
}

void dominoClientHubLog(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    dominoLogVa(g_logger_ptr, 0U, level, loc, fmt, arg_list);
    va_end(arg_list);
}

void dominoClientHubLogMsg(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* msg_ptr) {
    dominoLogMsg(g_logger_ptr, 0U, level, loc, msg_ptr);
}
