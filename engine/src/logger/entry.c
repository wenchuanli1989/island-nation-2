#include "entry.h"

#include <stdarg.h>
#include <stddef.h>

#include "domino_shared_logger.h"

static DominoLogger g_logger;

void dominoLoggerModuleInit(void) {
    static const DominoLogModule modules[] = {
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_CORE, .name = "core"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_STORAGE, .name = "storage"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_COMMON, .name = "common"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_HOST, .name = "host"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_HUMAN, .name = "human"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_NAV, .name = "nav"},
        {.module_id = (domino_log_module_id_t)DOMINO_ENGINE_LOG_MODULE_SOCIAL, .name = "social"},
    };
    dominoLogInit((DominoLogConfig){0}, modules, sizeof(modules) / sizeof(modules[0]), &g_logger);
}

void dominoLoggerModuleStart(void) {
    dominoLogStart(&g_logger);
}

void dominoLoggerModuleStop(void) {
    dominoLogStop(&g_logger);
}

void dominoLoggerModuleDestroy(void) {
    dominoLogDestroy(&g_logger);
}

void dominoEngineLog(DOMINO_ENGINE_LOG_MODULE module, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt, ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    dominoLogVa(&g_logger, (domino_log_module_id_t)module, level, loc, fmt, arg_list);
    va_end(arg_list);
}
