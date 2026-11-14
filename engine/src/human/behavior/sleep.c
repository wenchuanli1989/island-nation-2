#include "sleep.h"

#include "../rules.h"
void dominoSleepCalculate(DominoBehavior* value, DominoNeedValues* needs, domino_runtime_ms_t begin, domino_runtime_ms_t end) {
    uint32_t span = dominoHumanTimeSpan(begin, end);
    if (value->status != DOMINO_BEHAVIOR_STATUS_ACTIVE)
        return;
    uint32_t remaining = dominoHumanTimeSpan(value->active_elapsed_ms, value->plan.duration_ms);
    if (span > remaining)
        span = remaining;
    uint32_t phase = value->effect_phase_ms + span % DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS;
    uint32_t points = span / DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS + phase / DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS;
    value->effect_phase_ms = phase % DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS;
    value->active_elapsed_ms = dominoHumanTimeAdd(value->active_elapsed_ms, span);
    uint32_t gap = DOMINO_HUMAN_NEED_MAX - needs->energy;
    needs->energy += (uint8_t)(points < gap ? points : gap);
}
