#include "rules.h"

#include "domino_shared_human.h"

static_assert(DOMINO_HUMAN_NEED_MINUTE_MS > 0U, "Natural need interval must be positive");
static_assert(DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE > 0U, "Sleep trigger prediction requires positive natural decay");
static_assert(DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY < DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY, "Sleep trigger must be below its recovery target");
static_assert(DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY <= DOMINO_HUMAN_NEED_MAX, "Sleep recovery target must fit the need range");
static_assert(DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS > 0U, "Sleep recovery interval must be positive");
static_assert(DOMINO_HUMAN_NEED_MINUTE_MS % DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS == 0U, "Sleep estimation requires whole points per minute");
static_assert(DOMINO_HUMAN_NEED_MINUTE_MS / DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS > DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE,
              "Sleep must restore energy faster than natural decay");

enum {
    SLEEP_ENERGY_NET_PER_MINUTE = DOMINO_HUMAN_NEED_MINUTE_MS / DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS - DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE,
};

static_assert((DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY - 1U) / SLEEP_ENERGY_NET_PER_MINUTE + 1U <= UINT32_MAX / DOMINO_HUMAN_NEED_MINUTE_MS,
              "Maximum sleep duration must fit runtime milliseconds");

domino_runtime_ms_t dominoHumanEstimateSleepMs(uint8_t energy) {
    const uint32_t missing_energy = DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY - energy;
    const uint32_t minute_count = (missing_energy - 1U) / SLEEP_ENERGY_NET_PER_MINUTE + 1U;
    return minute_count * DOMINO_HUMAN_NEED_MINUTE_MS;
}
