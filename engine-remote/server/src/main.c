#include <stdlib.h>

#include "logger/entry.h"

int main(void) {
    dominoEngineRemoteServerLoggerInit((DominoLogConfig){0});
    dominoEngineRemoteServerLoggerStart();

    DOMINO_ENGINE_REMOTE_SERVER_LOG(DOMINO_LOG_LEVEL_INFO, "hello world");
    dominoEngineRemoteServerLoggerStop();
    dominoEngineRemoteServerLoggerDestroy();
    return EXIT_SUCCESS;
}
