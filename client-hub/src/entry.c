#include "logger/entry.h"

#include "domino_client_hub.h"

int domino_client_hub_run(void) {
    DOMINO_CODE result = dominoClientHubLoggerInit((DominoLogConfig){0});
    if (result != CODE_OK) {
        return result;
    }

    DOMINO_CLIENT_HUB_LOG_MSG(DOMINO_LOG_LEVEL_INFO, "domino client hub run");
    dominoClientHubLoggerExit();
    return CODE_OK;
}
