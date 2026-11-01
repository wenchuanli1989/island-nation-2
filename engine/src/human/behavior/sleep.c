#include "sleep.h"

#include "../rules.h"

void dominoHumanApplySleep(DominoHumanData* data_ptr, DominoBehavior* behavior_ptr, domino_runtime_ms_t now_ms) {
    const domino_runtime_ms_t old_accounted_ms = behavior_ptr->accounted_at_ms;
    if (now_ms < old_accounted_ms) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "sleep settlement time precedes progress for behavior=%u", behavior_ptr->behavior_id);
    }
    const domino_runtime_ms_t remaining_ms = dominoHumanGetRemainingSleepMs(behavior_ptr);
    const domino_runtime_ms_t elapsed_ms = now_ms - old_accounted_ms;
    const domino_runtime_ms_t span_ms = elapsed_ms < remaining_ms ? elapsed_ms : remaining_ms;
    const uint32_t old_points = behavior_ptr->active_elapsed_ms / DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS;
    behavior_ptr->active_elapsed_ms += span_ms;
    const uint32_t gained_points = behavior_ptr->active_elapsed_ms / DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS - old_points;
    const uint32_t missing_points = DOMINO_HUMAN_NEED_MAX - data_ptr->energy;
    data_ptr->energy += (uint8_t)(gained_points < missing_points ? gained_points : missing_points);
    behavior_ptr->accounted_at_ms = now_ms;
    if (span_ms == remaining_ms) {
        behavior_ptr->status = DOMINO_BEHAVIOR_STATUS_DONE;
        behavior_ptr->ended_at_ms = now_ms;
    }
}
