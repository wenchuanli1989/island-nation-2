#ifndef DOMINO_ENGINE_DISPATCH_MODULE_H
#define DOMINO_ENGINE_DISPATCH_MODULE_H

#include "domino_engine.h"

/** @file entry.h
 * @brief 客户端消息分派：主循环按消息类型把客户端消息交给对应业务场景。当前尚未定义业务场景。
 */

/** @brief 分派一条主循环已出队的客户端消息。
 * @return 目前没有已定义的类型：全部消息记录一次后返回 ERR_INVALID_MESSAGE，不回包。
 */
extern DOMINO_CODE dominoDispatchClientMessage(const DominoEngineMessage* message_ptr);

#endif
