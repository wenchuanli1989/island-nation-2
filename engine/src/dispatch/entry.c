#include "entry.h"

DOMINO_CODE dominoDispatchMessage(const DominoEngineMessage* message_ptr) {
    if (message_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }

    switch (message_ptr->type) {
        case DOMINO_ENGINE_MSG_NONE:
            return CODE_OK;
        default:
            return ERR_INVALID_MESSAGE;
    }
}
