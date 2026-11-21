#ifndef DOMINO_ENGINE_PROTOCOL_H
#define DOMINO_ENGINE_PROTOCOL_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/** @file domino_engine_protocol.h
 * @brief 客户端收发的消息结构和消息类型，只依赖 C 标准库。
 * @note 每个业务场景单独定义请求和结果类型，参数针对场景设计；新增场景时在 id 与 type 之间加入 union payload。
 * 不提供任务、规划请求或通用实体字段读写类型（宪法 3.6、第十章）。
 */

/** @brief 客户端消息类型；当前尚未定义业务场景。 */
typedef enum DOMINO_ENGINE_MESSAGE_TYPE {
    DOMINO_ENGINE_MESSAGE_NONE = 0,  ///< 未设置的类型，不合法。
} DOMINO_ENGINE_MESSAGE_TYPE;

/** @brief 客户端与主循环之间的消息；目前只有队列 ID 和类型。 */
typedef struct DominoEngineMessage {
    uint64_t id;    ///< 入队时由队列回写的消息 ID。
    uint32_t type;  ///< DOMINO_ENGINE_MESSAGE_TYPE。
} DominoEngineMessage;

static_assert(offsetof(DominoEngineMessage, id) == 0, "DominoEngineMessage id must be first for queue ID writeback");
static_assert(sizeof(DominoEngineMessage) <= 256U, "engine message must stay well within one MTU");

#endif
