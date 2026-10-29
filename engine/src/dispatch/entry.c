#include "entry.h"

#include "../logger/entry.h"

DOMINO_CODE dominoDispatchMessage(const DominoEngineMessage* message_ptr) {
    switch (message_ptr->type) {
        case DOMINO_ENGINE_MSG_NONE:
            return CODE_OK;
        default:
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "unsupported message type=%u (code=%d)", message_ptr->type,
                              ERR_INVALID_MESSAGE);
            return ERR_INVALID_MESSAGE;
    }
}
