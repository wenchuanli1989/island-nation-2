#ifndef DOMINO_ENGINE_HUMAN_RULES_H
#define DOMINO_ENGINE_HUMAN_RULES_H

#include <stdint.h>

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

/** @brief 按每完整现实分钟净恢复 9 分精力，估算新睡眠计划的有效目标时长。
 * @param energy 已校验的当前精力，须小于 DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY。
 * @return 由规则参数保证落在 32 位毫秒范围内的正时长。
 * @note 内部纯计算入口；调用者负责精力业务条件及绝对到期时刻的加法范围检查。
 * 只用于新计划估算，不读取时钟，不修改人物，也不替代按真实时间边界进行的睡眠结算。
 */
domino_runtime_ms_t dominoHumanEstimateSleepMs(uint8_t energy);

#endif
