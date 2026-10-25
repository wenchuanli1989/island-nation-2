#ifndef DOMINO_ENGINE_DISPATCH_MODULE_H
#define DOMINO_ENGINE_DISPATCH_MODULE_H

#include "domino_engine.h"

/** @brief 当前仅接受 DOMINO_ENGINE_MSG_NONE，其他类型返回 ERR_INVALID_MESSAGE。
 * @pre message_ptr 指向主循环已成功出队的消息；空指针违反内部约定，直接打印到 stderr 并终止进程。
 */
extern DOMINO_CODE dominoDispatchMessage(const DominoEngineMessage* message_ptr);

#endif
