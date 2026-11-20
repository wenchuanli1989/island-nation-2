#ifndef DOMINO_ENGINE_DISPATCH_MODULE_H
#define DOMINO_ENGINE_DISPATCH_MODULE_H

#include "domino_engine.h"

/** @file entry.h
 * @brief 客户端接入层：按消息类型分派主循环收到的客户端消息，并编码回包。
 * @note 消息类型按业务场景单独定义，参数和返回数据结构针对该场景设计；不提供通用实体字段读写，
 * 也不路由任务请求、规划请求或规划结果，这些内容仅由引擎内部产生。当前尚未定义业务场景。
 */

/** @brief 主循环分发一条已出队的客户端消息。
 * @pre message_ptr 指向主循环已成功出队的非空消息；由调用方保证，本接口不重复判空。
 * @return 当前没有已定义的业务类型，全部消息记录一次后返回 ERR_INVALID_MESSAGE，不回包。
 */
extern DOMINO_CODE dominoDispatchMessage(const DominoEngineMessage* message_ptr);

#endif
