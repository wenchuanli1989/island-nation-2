#ifndef DOMINO_ENGINE_GAME_TIMER_MESSAGE_H
#define DOMINO_ENGINE_GAME_TIMER_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_queue.h"
#include "domino_shared_types.h"

/** @brief 64 位登记 ID，不是时间值；业务毫秒统一使用 32 位 domino_runtime_ms_t。 */
typedef uint64_t domino_game_timer_id_t;

/** @brief 登记命令、堆项及到期消息的计时载荷；到期时按值内嵌于 DominoThreadMessage。 */
typedef struct DominoGameTimer {
    domino_game_timer_id_t game_timer_id;  ///< 必须为首字段，由登记命令队列写入；本次 Init 生命周期内的稳定取消凭据。
    DominoThreadQueue* event_queue_ptr;    ///< 业务方拥有的通用收件队列，元素类型为 DominoThreadMessage。
    uint64_t data_id;                      ///< 业务标识，原样按值传递；game_timer 不解释其含义或持有业务对象。
    domino_runtime_ms_t due_ms;            ///< 到期时刻：累计未冻结现实时间达到此毫秒数时到期，即 now_ms >= due_ms。
    uint8_t event_type;
    bool cancelled_flag;  ///< 取消标志，登记接口初始化为 false，由 Cancel 按请求 ID 标记。
} DominoGameTimer;

static_assert(offsetof(DominoGameTimer, game_timer_id) == 0, "DominoGameTimer game_timer_id must be the first field for queue ID writeback");
static_assert(sizeof(DominoGameTimer) == 32, "DominoGameTimer size must be 32 bytes");

#endif
