#ifndef DOMINO_THREAD_MESSAGE_TEST_HELPERS_H
#define DOMINO_THREAD_MESSAGE_TEST_HELPERS_H

#include "timer/entry.h"

/** @brief 测试消费者按消息类型解包 timer，复制后释放载荷，保留独立的收件队列 ID。 */
static inline DOMINO_CODE testConsumeTimerMessage(DominoThreadQueue* queue_ptr, DominoTimer* timer_out_ptr, uint64_t* message_id_out_ptr) {
    DominoThreadMessage message;
    DOMINO_CODE code = dominoThreadQueueConsume(queue_ptr, &message, false);
    if (code != CODE_OK) {
        return code;
    }
    if (message.items_type != DOMINO_THREAD_MESSAGE_TIMER || message.items_size != sizeof(DominoTimer) || message.items_ptr == nullptr) {
        code = ERR_INVALID_MESSAGE;
    } else {
        *timer_out_ptr = *(const DominoTimer*)message.items_ptr;
        if (message_id_out_ptr != nullptr) {
            *message_id_out_ptr = message.id;
        }
    }
    dominoThreadMessageRelease(&message);
    return code;
}

/** @brief 所有生产者、消费者 join 后，排空尚未接收的载荷再销毁通用消息队列。 */
static inline void testDestroyMessageQueue(DominoThreadQueue* queue_ptr) {
    DominoThreadMessage message;
    while (dominoThreadQueueConsume(queue_ptr, &message, false) == CODE_OK) {
        dominoThreadMessageRelease(&message);
    }
    dominoThreadQueueDestroy(queue_ptr);
}

#endif
