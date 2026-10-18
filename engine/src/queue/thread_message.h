#ifndef DOMINO_ENGINE_THREAD_MESSAGE_H
#define DOMINO_ENGINE_THREAD_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"

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
 * @note 队列以 sizeof(DominoThreadMessage) 初始化，按值复制本描述符。
 *       items_ptr 指向独占、可用 free 释放的连续块；入队成功转移所有权，失败仍归发送者。
 *       出队后先检查 items_type 和 items_size，再转换 items_ptr；处理或丢弃后调用 Release。
 *       禁止传递栈地址或借用可移动容器内部元素。载荷内部指针的生命周期由业务协议约束。
 */
typedef struct DominoThreadMessage {
    uint64_t id;  ///< 所属收件队列回写的消息 ID，与载荷内部的业务 ID 独立。
    void* items_ptr;
    size_t items_size;
    DominoThreadMessageType items_type;
} DominoThreadMessage;

/**
 * @brief 分配独立连续块并复制载荷；payload_ptr 只需在本次调用期间有效。
 * @note message_ptr 不得已拥有载荷；成功后 id 为 0，失败不修改输出。复制不会递归复制载荷内部的指针。
 * @return 空指针返回 ERR_NULL_POINTER；无效消息类型或零长度返回 ERR_INVALID_PARAM；分配失败返回 ERR_MEMORY_ALLOC。
 */
DOMINO_CODE dominoThreadMessageInitCopy(DominoThreadMessage* message_ptr, DominoThreadMessageType items_type, const void* payload_ptr,
                                        size_t items_size);

/**
 * @brief 释放当前拥有的载荷并清空描述符；nullptr 或空描述符可安全调用。
 * @note 仅接收者、尚未发送或发送失败的发送者可调用；发送成功后不得再读取或释放已转移的 items_ptr。
 *       仅释放顶层连续块，业务须先处理载荷内部另行拥有的资源。
 */
void dominoThreadMessageRelease(DominoThreadMessage* message_ptr);

#endif
