#include "needs.h"

#include "rules.h"

static_assert(DOMINO_HUMAN_NEED_MINUTE_MS == 60000U, "dominoHumanNextNeedTickMs assumes a one minute natural period");

/** @brief 衰减 tick_count 次，降到 0 为止；先比较可扣次数，避免乘法溢出和下溢。 */
static inline uint8_t decayNeed(uint8_t value, uint32_t tick_count, uint32_t decay_per_tick) {
    if (decay_per_tick == 0U || tick_count == 0U) {
        return value;
    }
    if (tick_count > (uint32_t)value / decay_per_tick) {
        return 0U;
    }
    return (uint8_t)((uint32_t)value - (tick_count * decay_per_tick));
}

DominoHumanNeedValues dominoHumanReadNeeds(const DominoHumanData* data_ptr) {
    return (DominoHumanNeedValues){
        .energy = data_ptr->energy,
        .food = data_ptr->food,
        .water = data_ptr->water,
        .hygiene = data_ptr->hygiene,
        .recreation = data_ptr->recreation,
        .social = data_ptr->social,
    };
}

void dominoHumanWriteNeeds(DominoHumanData* data_ptr, const DominoHumanNeedValues* values_ptr) {
    data_ptr->energy = values_ptr->energy;
    data_ptr->food = values_ptr->food;
    data_ptr->water = values_ptr->water;
    data_ptr->hygiene = values_ptr->hygiene;
    data_ptr->recreation = values_ptr->recreation;
    data_ptr->social = values_ptr->social;
}

DominoHumanNeedValues dominoHumanCalcNaturalNeeds(const DominoHumanNeedValues* values_ptr, domino_runtime_ms_t begin_ms, domino_runtime_ms_t end_ms) {
    const uint32_t tick_count = (end_ms / DOMINO_HUMAN_NEED_MINUTE_MS) - (begin_ms / DOMINO_HUMAN_NEED_MINUTE_MS);
    return (DominoHumanNeedValues){
        .energy = decayNeed(values_ptr->energy, tick_count, DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE),
        .food = decayNeed(values_ptr->food, tick_count, DOMINO_HUMAN_FOOD_DECAY_PER_MINUTE),
        .water = decayNeed(values_ptr->water, tick_count, DOMINO_HUMAN_WATER_DECAY_PER_MINUTE),
        .hygiene = decayNeed(values_ptr->hygiene, tick_count, DOMINO_HUMAN_HYGIENE_DECAY_PER_MINUTE),
        .recreation = decayNeed(values_ptr->recreation, tick_count, DOMINO_HUMAN_RECREATION_DECAY_PER_MINUTE),
        .social = decayNeed(values_ptr->social, tick_count, DOMINO_HUMAN_SOCIAL_DECAY_PER_MINUTE),
    };
}

uint64_t dominoHumanNaturalMsUntilEnergyAtMost(uint8_t energy, domino_runtime_ms_t from_ms, uint8_t threshold) {
    if (energy <= threshold) {
        return 0U;
    }
    const uint32_t gap = (uint32_t)energy - threshold;
    const uint64_t tick_count = ((gap - 1U) / DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE) + 1U;
    return (uint64_t)dominoHumanMsUntilNeedTick(from_ms) + ((tick_count - 1U) * DOMINO_HUMAN_NEED_MINUTE_MS);
}
