#include "entry.h"

#include <stdarg.h>

static DominoLogger g_logger;

void dominoEngineRemoteLocalLoggerInit(DominoLogConfig config) {
    static const DominoLogModule modules[] = {
        {.module_id = 0U, .name = "remote-local"},
    };
    dominoLogInit(config, modules, sizeof(modules) / sizeof(modules[0]), &g_logger);
}

void dominoEngineRemoteLocalLoggerStart(void) {
    dominoLogStart(&g_logger);
}

void dominoEngineRemoteLocalLoggerStop(void) {
    dominoLogStop(&g_logger);
}

void dominoEngineRemoteLocalLoggerDestroy(void) {
    dominoLogDestroy(&g_logger);
}

void dominoEngineRemoteLocalLog(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    dominoLogVa(&g_logger, 0U, level, loc, fmt, arg_list);
    va_end(arg_list);
}
