#include "logger/entry.h"

#include "domino_engine_remote_local.h"
#include "domino_shared_common.h"

void dominoEngineRemoteLocalInit(void) {
    dominoEngineRemoteLocalLoggerInit((DominoLogConfig){0});
}

void dominoEngineRemoteLocalStart(void) {
    dominoEngineRemoteLocalLoggerStart();
    DOMINO_PRINT_MESSAGE("domino engine remote local started");
}

void dominoEngineRemoteLocalStop(void) {
    dominoEngineRemoteLocalLoggerStop();
}

void dominoEngineRemoteLocalDestroy(void) {
    dominoEngineRemoteLocalLoggerDestroy();
}
