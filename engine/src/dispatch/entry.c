#include "entry.h"

#include "../logger/entry.h"

DOMINO_CODE dominoDispatchClientMessage(const DominoEngineMessage* message_ptr) {
    // 新增业务场景时在此按类型分派；未定义的类型（含 NONE）一律拒绝。
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "unsupported message type=%u (code=%d)", message_ptr->type,
                      ERR_INVALID_MESSAGE);
    return ERR_INVALID_MESSAGE;
}
