#ifndef DOMINO_ENGINE_TIMER_MESSAGE_H
#define DOMINO_ENGINE_TIMER_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_queue.h"
#include "domino_shared_types.h"

/** @brief 64 位登记 ID，不是时间值；业务毫秒统一使用 32 位 domino_runtime_ms_t。 */
typedef uint64_t domino_timer_id_t;

/** @brief 登记命令、堆项及到期消息的计时载荷；到期时按值内嵌于 DominoThreadMessage。 */
typedef struct DominoTimer {
    domino_timer_id_t timer_id;          ///< 必须为首字段，由登记命令队列写入；本次 Init 生命周期内的稳定取消凭据。
    DominoThreadQueue* event_queue_ptr;  ///< 业务方拥有的通用收件队列，元素类型为 DominoThreadMessage。
    domino_runtime_ms_t due_ms;          ///< 到期时刻：累计未冻结现实时间达到此毫秒数时到期，即 now_ms >= due_ms。
    /**
     * @brief 登记时要等待的毫秒数，冻结时间不计入；登记后保持不变，不随时间递减。
     * @note Schedule 由调用方填写；ScheduleAt 根据到期时刻计算剩余时长，已到期则写 0。
     * @note 0 表示尽快异步投递。
     */
    domino_runtime_ms_t delay_ms;
    uint8_t event_type;
    bool cancelled_flag;  ///< 取消标志，正常登记时初始化为 false；登记接口拒绝已标记取消的请求。
} DominoTimer;

static_assert(offsetof(DominoTimer, timer_id) == 0, "DominoTimer timer_id must be the first field for queue ID writeback");
static_assert(sizeof(DominoTimer) == 32, "DominoTimer size must be 32 bytes");

#endif
