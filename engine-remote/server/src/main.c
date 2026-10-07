#include <stdlib.h>

#include "logger/entry.h"

int main(void) {
    if (dominoEngineRemoteServerLoggerInit((DominoLogConfig){0}) != CODE_OK) {
        return EXIT_FAILURE;
    }

    DOMINO_ENGINE_REMOTE_SERVER_LOG_MSG(DOMINO_LOG_LEVEL_INFO, "hello world");
    dominoEngineRemoteServerLoggerExit();
    return EXIT_SUCCESS;
}
