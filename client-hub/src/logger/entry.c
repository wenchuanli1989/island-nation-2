#include "entry.h"

#include <stdarg.h>

static DominoLogger g_logger;

void dominoClientHubLoggerInit(DominoLogConfig config) {
    static const DominoLogModule modules[] = {
        {.module_id = 0U, .name = "client-hub"},
    };
    dominoLogInit(config, modules, sizeof(modules) / sizeof(modules[0]), &g_logger);
}

void dominoClientHubLoggerStart(void) {
    dominoLogStart(&g_logger);
}

void dominoClientHubLoggerStop(void) {
    dominoLogStop(&g_logger);
}

void dominoClientHubLoggerDestroy(void) {
    dominoLogDestroy(&g_logger);
}

void dominoClientHubLog(DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    dominoLogVa(&g_logger, 0U, level, loc, fmt, arg_list);
    va_end(arg_list);
}
