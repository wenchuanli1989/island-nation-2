#ifndef DOMINO_ENGINE_PROTOCOL_H
#define DOMINO_ENGINE_PROTOCOL_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/** @file domino_engine_protocol.h
 * @brief 客户端收发消息结构及按业务场景定义的消息类型。
 * @note 只依赖 C 标准库头文件，不包含 engine 或 shared 的内部结构。引擎不提供通用的内部数据读写：不按实体类别、字段 ID
 * 或内部结构体布局开放读取或写入。每个业务场景单独定义消息类型，参数和返回数据结构针对该场景设计；新增场景时在
 * DominoEngineMsgType 增加该场景的请求及结果类型，并在 id 与 type 之间加入 union payload 承载各场景载荷（多类型统一布局）。
 * 消息经 domino_engine.h 的 dominoEnginePushMsg／dominoEnginePopMsg 收发。
 */

/** @brief 客户端收发队列的消息类型；当前尚未定义业务场景，主循环把收到的消息全部按不支持的类型记录并拒绝。
 * @note 不提供任务请求、规划请求或规划结果类型，也不提供通用实体字段读写类型。
 */
typedef enum DominoEngineMsgType {
    DOMINO_ENGINE_MSG_NONE = 0,  ///< 未设置的类型，不合法。
} DominoEngineMsgType;

/** @brief 客户端与主循环之间的对外队列消息；新增业务场景前只有队列 ID 和类型。 */
typedef struct DominoEngineMessage {
    uint64_t id;    ///< 入队成功时由所属队列生成并回写的消息 ID。
    uint32_t type;  ///< DominoEngineMsgType。
} DominoEngineMessage;

static_assert(offsetof(DominoEngineMessage, id) == 0, "DominoEngineMessage id must be first for queue ID writeback");
static_assert(sizeof(DominoEngineMessage) <= 256U, "engine message must stay well within one MTU");

#endif
