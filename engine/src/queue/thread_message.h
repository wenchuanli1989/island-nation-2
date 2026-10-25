#ifndef DOMINO_ENGINE_THREAD_MESSAGE_H
#define DOMINO_ENGINE_THREAD_MESSAGE_H

#include <stdint.h>

#include "../human/message.h"
#include "../timer/message.h"

/** @brief 通用消息分类；AI 与命令的具体载荷由对应业务模块定义。 */
typedef enum DominoThreadMessageType {
    DOMINO_THREAD_MESSAGE_NONE = 0,
    DOMINO_THREAD_MESSAGE_TIMER,
    DOMINO_THREAD_MESSAGE_AI_REQUEST,
    DOMINO_THREAD_MESSAGE_AI_RESULT,
    DOMINO_THREAD_MESSAGE_COMMAND,
} DominoThreadMessageType;

/**
 * @brief 线程收件队列的统一元素；同一队列可混合保存不同类型的载荷。
 * @note id 必须位于首字段，供通用队列回写；队列以 sizeof(DominoThreadMessage) 初始化，按值复制整条消息。
 *       type 决定 payload 的有效成员；发送者直接初始化对应成员，无需分配或释放外层载荷。
 *       AI 草案中的行为块仍为独占资源，入队成功转移所有权，失败仍归发送者；消费者处理或丢弃后按业务协议释放。
 *       载荷内部指针不会递归复制，不得指向发送者栈或可移动容器元素；timer 的队列指针由业务方保证生命周期。
 */
typedef struct DominoThreadMessage {
    uint64_t id;  ///< 所属收件队列回写的消息 ID，与载荷内部的业务 ID 独立。
    union {
        DominoHumanPlanEnvelope plan;
        DominoTimer timer;
        HumanExecutionCommand command;
    } payload;
    DominoThreadMessageType type;
} DominoThreadMessage;

#endif
