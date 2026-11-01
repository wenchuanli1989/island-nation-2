#include "needs.h"

#include "domino_shared_common.h"
#include "rules.h"

static_assert((DOMINO_HUMAN_NEED_MAX - DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY - 1U) / DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE + 1U <=
                  UINT32_MAX / DOMINO_HUMAN_NEED_MINUTE_MS,
              "Maximum sleep trigger interval must fit runtime milliseconds");

domino_runtime_ms_t dominoHumanGetSleepTriggerMs(const DominoHumanData* data_ptr) {
    const uint32_t energy_gap = data_ptr->energy - DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY;
    const uint32_t minute_count = (energy_gap - 1U) / DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE + 1U;
    const domino_runtime_ms_t delta_ms = minute_count * DOMINO_HUMAN_NEED_MINUTE_MS;
    if (delta_ms > UINT32_MAX - data_ptr->needs_updated_at_ms) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "sleep trigger time overflows for human=%u", data_ptr->human_id);
    }
    return data_ptr->needs_updated_at_ms + delta_ms;
}

void dominoHumanApplyNaturalNeeds(DominoHumanData* data_ptr, domino_runtime_ms_t now_ms) {
    const domino_runtime_ms_t elapsed_ms = now_ms - data_ptr->needs_updated_at_ms;
    const uint32_t minute_count = elapsed_ms / DOMINO_HUMAN_NEED_MINUTE_MS;
    if (minute_count == 0U) {
        return;
    }

    data_ptr->energy = dominoHumanDecayNeed(data_ptr->energy, minute_count, DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE);
    data_ptr->food = dominoHumanDecayNeed(data_ptr->food, minute_count, DOMINO_HUMAN_FOOD_DECAY_PER_MINUTE);
    data_ptr->water = dominoHumanDecayNeed(data_ptr->water, minute_count, DOMINO_HUMAN_WATER_DECAY_PER_MINUTE);
    data_ptr->hygiene = dominoHumanDecayNeed(data_ptr->hygiene, minute_count, DOMINO_HUMAN_HYGIENE_DECAY_PER_MINUTE);
    data_ptr->recreation = dominoHumanDecayNeed(data_ptr->recreation, minute_count, DOMINO_HUMAN_RECREATION_DECAY_PER_MINUTE);
    data_ptr->social = dominoHumanDecayNeed(data_ptr->social, minute_count, DOMINO_HUMAN_SOCIAL_DECAY_PER_MINUTE);
    data_ptr->needs_updated_at_ms = now_ms - elapsed_ms % DOMINO_HUMAN_NEED_MINUTE_MS;
}
