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

#include "domino_shared_common.h"
/** @brief 所有业务时间加减都先校验，不回绕或饱和。 */
static inline domino_runtime_ms_t dominoHumanTimeAdd(domino_runtime_ms_t base, domino_runtime_ms_t delta) {
    if (delta > UINT32_MAX - base)
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "human business time overflow");
    return base + delta;
}
static inline domino_runtime_ms_t dominoHumanTimeSpan(domino_runtime_ms_t begin, domino_runtime_ms_t end) {
    if (end < begin)
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "human business time reversed");
    return end - begin;
}
#endif
