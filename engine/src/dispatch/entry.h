#ifndef DOMINO_ENGINE_DISPATCH_MODULE_H
#define DOMINO_ENGINE_DISPATCH_MODULE_H

#include "domino_engine.h"

/** @brief 当前仅接受 DOMINO_ENGINE_MSG_NONE，其他类型返回 ERR_INVALID_MESSAGE。 */
extern DOMINO_CODE dominoDispatchMessage(const DominoEngineMessage* message_ptr);

#endif
