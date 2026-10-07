#include "logger/entry.h"

#include "domino_engine_remote_local.h"

int domino_engine_remote_local_run(void) {
    DOMINO_CODE result = dominoEngineRemoteLocalLoggerInit((DominoLogConfig){0});
    if (result != CODE_OK) {
        return result;
    }

    DOMINO_ENGINE_REMOTE_LOCAL_LOG_MSG(DOMINO_LOG_LEVEL_INFO, "domino engine remote local run");
    dominoEngineRemoteLocalLoggerExit();
    return CODE_OK;
}
