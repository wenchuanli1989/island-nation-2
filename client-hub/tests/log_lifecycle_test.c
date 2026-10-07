#include <stdio.h>
#include <stdlib.h>

#include "../../engine-remote/local/src/logger/entry.h"
#include "../../engine-remote/server/src/logger/entry.h"
#include "../../engine/src/logger/entry.h"
#include "../src/logger/entry.h"
#include "domino_client_hub.h"
#include "domino_engine_remote_local.h"

/** @brief 四个模块的日志适配器可共存，退出或重新运行一个模块不改变其他实例的生命周期。 */
int main(void) {
    int exit_code = EXIT_FAILURE;
    DominoLogConfig config = {.buffer_size = 2U};

#define TEST_CHECK(condition)                                                       \
    do {                                                                            \
        if (!(condition)) {                                                         \
            (void)fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); \
            goto cleanup;                                                           \
        }                                                                           \
    } while (0)

    TEST_CHECK(dominoLoggerModuleInit() == CODE_OK);
    TEST_CHECK(dominoClientHubLoggerInit(config) == CODE_OK);
    TEST_CHECK(dominoEngineRemoteLocalLoggerInit(config) == CODE_OK);
    TEST_CHECK(dominoEngineRemoteServerLoggerInit(config) == CODE_OK);

    TEST_CHECK(dominoLoggerModuleInit() == ERR_ALREADY_INITIALIZED);
    TEST_CHECK(dominoClientHubLoggerInit(config) == ERR_ALREADY_INITIALIZED);
    TEST_CHECK(dominoEngineRemoteLocalLoggerInit(config) == ERR_ALREADY_INITIALIZED);
    TEST_CHECK(dominoEngineRemoteServerLoggerInit(config) == ERR_ALREADY_INITIALIZED);
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_INFO, "coexisting engine %d", 1);
    DOMINO_CLIENT_HUB_LOG(DOMINO_LOG_LEVEL_INFO, "coexisting client hub %d", 2);
    DOMINO_ENGINE_REMOTE_LOCAL_LOG(DOMINO_LOG_LEVEL_INFO, "coexisting remote local %d", 3);
    DOMINO_ENGINE_REMOTE_SERVER_LOG(DOMINO_LOG_LEVEL_INFO, "coexisting remote server %d", 4);

    dominoClientHubLoggerExit();
    TEST_CHECK(domino_client_hub_run() == CODE_OK);
    TEST_CHECK(dominoLoggerModuleInit() == ERR_ALREADY_INITIALIZED);
    TEST_CHECK(dominoEngineRemoteLocalLoggerInit(config) == ERR_ALREADY_INITIALIZED);
    TEST_CHECK(dominoEngineRemoteServerLoggerInit(config) == ERR_ALREADY_INITIALIZED);

    dominoEngineRemoteLocalLoggerExit();
    TEST_CHECK(domino_engine_remote_local_run() == CODE_OK);
    TEST_CHECK(dominoLoggerModuleInit() == ERR_ALREADY_INITIALIZED);
    TEST_CHECK(dominoEngineRemoteServerLoggerInit(config) == ERR_ALREADY_INITIALIZED);
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_INFO, "engine survives hub and local exit");
    DOMINO_ENGINE_REMOTE_SERVER_LOG_MSG(DOMINO_LOG_LEVEL_INFO, "server survives hub and local exit");

    dominoLoggerModuleExit();
    TEST_CHECK(dominoClientHubLoggerInit(config) == CODE_OK);
    TEST_CHECK(dominoEngineRemoteLocalLoggerInit(config) == CODE_OK);
    DOMINO_CLIENT_HUB_LOG_MSG(DOMINO_LOG_LEVEL_INFO, "hub restarts without engine logger");
    DOMINO_ENGINE_REMOTE_LOCAL_LOG_MSG(DOMINO_LOG_LEVEL_INFO, "local restarts without engine logger");
    TEST_CHECK(dominoLoggerModuleInit() == CODE_OK);
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_INFO, "engine restarts alongside other loggers");
    exit_code = EXIT_SUCCESS;

cleanup:
    dominoClientHubLoggerExit();
    dominoEngineRemoteLocalLoggerExit();
    dominoEngineRemoteServerLoggerExit();
    dominoLoggerModuleExit();
    return exit_code;

#undef TEST_CHECK
}
