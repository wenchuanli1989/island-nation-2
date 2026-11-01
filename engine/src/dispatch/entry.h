#ifndef DOMINO_ENGINE_DISPATCH_MODULE_H
#define DOMINO_ENGINE_DISPATCH_MODULE_H

#include "domino_engine.h"

/** @brief 当前仅接受 DOMINO_ENGINE_MSG_NONE，其他类型返回 ERR_INVALID_MESSAGE。
 * @note 客户端消息不路由任务请求、规划请求或规划结果；这些内容仅由引擎内部产生。
 * @pre message_ptr 指向主循环已成功出队的非空消息；由调用方保证，本接口不重复判空。
 */
extern DOMINO_CODE dominoDispatchMessage(const DominoEngineMessage* message_ptr);

#endif
