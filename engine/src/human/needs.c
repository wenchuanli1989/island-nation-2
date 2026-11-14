#include "needs.h"

#include "rules.h"
DominoNeedValues dominoHumanReadNeeds(const DominoHumanData* data) {
    return (DominoNeedValues){data->needs_phase_ms, data->energy, data->food, data->water, data->hygiene, data->recreation, data->social};
}
void dominoHumanWriteNeeds(DominoHumanData* data, DominoNeedValues values) {
    data->needs_phase_ms = values.phase_ms;
    data->energy = values.energy;
    data->food = values.food;
    data->water = values.water;
    data->hygiene = values.hygiene;
    data->recreation = values.recreation;
    data->social = values.social;
}
static uint8_t decay(uint8_t value, uint32_t count, uint32_t rate) {
    return count > value / rate ? 0 : (uint8_t)(value - count * rate);
}
DominoNeedValues dominoHumanCalculateNeeds(DominoNeedValues values, domino_runtime_ms_t begin_ms, domino_runtime_ms_t end_ms) {
    uint32_t span = dominoHumanTimeSpan(begin_ms, end_ms);
    // 除法分解避免相位加到整段时长发生溢出。
    uint32_t phase = values.phase_ms + span % DOMINO_HUMAN_NEED_MINUTE_MS;
    uint32_t count = span / DOMINO_HUMAN_NEED_MINUTE_MS + phase / DOMINO_HUMAN_NEED_MINUTE_MS;
    values.phase_ms = phase % DOMINO_HUMAN_NEED_MINUTE_MS;
    values.energy = decay(values.energy, count, DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE);
    values.food = decay(values.food, count, DOMINO_HUMAN_FOOD_DECAY_PER_MINUTE);
    values.water = decay(values.water, count, DOMINO_HUMAN_WATER_DECAY_PER_MINUTE);
    values.hygiene = decay(values.hygiene, count, DOMINO_HUMAN_HYGIENE_DECAY_PER_MINUTE);
    values.recreation = decay(values.recreation, count, DOMINO_HUMAN_RECREATION_DECAY_PER_MINUTE);
    values.social = decay(values.social, count, DOMINO_HUMAN_SOCIAL_DECAY_PER_MINUTE);
    return values;
}
