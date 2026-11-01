#ifndef DOMINO_ENGINE_HUMAN_RULES_H
#define DOMINO_ENGINE_HUMAN_RULES_H

#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

/** @brief 首版可调整的整数需求与睡眠参数；全部时长使用未冻结现实毫秒，不应用游戏日历倍率。 */
enum {
    DOMINO_HUMAN_NEED_MINUTE_MS = 60000U,
    DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_FOOD_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_WATER_DECAY_PER_MINUTE = 2U,
    DOMINO_HUMAN_HYGIENE_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_RECREATION_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_SOCIAL_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY = 30U,
    DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY = 80U,
    DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS = 6000U,
};

/** @brief 统一检查睡眠恢复条件，按每完整现实分钟净恢复 9 分精力计算绝对结束时刻。
 * @param energy 已保证在 0..100 内的当前精力。
 * @param now_ms 本次睡眠开始的累计未冻结现实毫秒。
 * @param end_ms_out_ptr 非空输出；成功时完整写入结束时刻，失败保持不变。
 * @return CODE_OK 成功；精力已满足返回 ERR_GAME_STATE_INVALID，结束时刻溢出返回 ERR_OUT_OF_RANGE。
 * @note 内部纯计算入口，供草案构造、结果接收与任务启动各自检查当前条件；新计划的触发阈值由构造器检查。
 * 不读取时钟、不修改人物，也不替代按真实时间边界进行的睡眠结算。
 */
DOMINO_CODE dominoHumanPrepareSleep(uint8_t energy, domino_runtime_ms_t now_ms, domino_runtime_ms_t* end_ms_out_ptr);

#endif
