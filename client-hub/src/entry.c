#include "logger/entry.h"

#include "domino_client_hub.h"
#include "domino_shared_common.h"

void dominoClientHubInit(void) {
    dominoClientHubLoggerInit((DominoLogConfig){0});
}

void dominoClientHubStart(void) {
    dominoClientHubLoggerStart();
    DOMINO_PRINT_MESSAGE("domino client hub started");
}

void dominoClientHubStop(void) {
    dominoClientHubLoggerStop();
}

void dominoClientHubDestroy(void) {
    dominoClientHubLoggerDestroy();
}
