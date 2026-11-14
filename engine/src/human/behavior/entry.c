#include "entry.h"

#include "../rules.h"
#include "sleep.h"
#ifdef DOMINO_PLAYGROUND_MODE
bool dominoBehaviorFixtureSupported(DOMINO_BEHAVIOR_TYPE type);
void dominoBehaviorFixtureCalculate(DominoBehavior* value, DominoNeedValues* needs, domino_runtime_ms_t begin, domino_runtime_ms_t end);
#endif
bool dominoBehaviorPlanValid(const DominoBehaviorPlan* plan) {
    if (!plan->duration_ms)
        return false;
    if (plan->type == DOMINO_BEHAVIOR_TYPE_SLEEP)
        return true;
#ifdef DOMINO_PLAYGROUND_MODE
    return dominoBehaviorFixtureSupported(plan->type);
#else
    return false;
#endif
}
bool dominoBehaviorCanStart(const DominoBehaviorPlan* plan, const DominoHumanData* data) {
    return dominoBehaviorPlanValid(plan) && (plan->type != DOMINO_BEHAVIOR_TYPE_SLEEP || data->sleep_allowed);
}
void dominoBehaviorCalculate(DominoBehavior* value, DominoNeedValues* needs, domino_runtime_ms_t begin, domino_runtime_ms_t end) {
    if (value->plan.type == DOMINO_BEHAVIOR_TYPE_SLEEP) {
        dominoSleepCalculate(value, needs, begin, end);
        return;
    }
#ifdef DOMINO_PLAYGROUND_MODE
    dominoBehaviorFixtureCalculate(value, needs, begin, end);
#else
    DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "unsupported executing behavior");
#endif
}
domino_runtime_ms_t dominoBehaviorNextDelta(const DominoBehavior* behavior) {
    uint32_t remaining = dominoHumanTimeSpan(behavior->active_elapsed_ms, behavior->plan.duration_ms);
    uint32_t period = behavior->plan.type == DOMINO_BEHAVIOR_TYPE_SLEEP ? DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS : 1000U;
    uint32_t next = period - behavior->effect_phase_ms;
    return next < remaining ? next : remaining;
}
DOMINO_BEHAVIOR_STATUS dominoBehaviorDecide(const DominoBehavior* behavior) {
    if (behavior->status >= DOMINO_BEHAVIOR_STATUS_SUCCEEDED)
        return behavior->status;
    return behavior->active_elapsed_ms == behavior->plan.duration_ms ? DOMINO_BEHAVIOR_STATUS_SUCCEEDED : behavior->status;
}
void dominoBehaviorFinish(DominoBehavior* behavior, DOMINO_BEHAVIOR_STATUS status, domino_runtime_ms_t now) {
    behavior->status = status;
    behavior->ended_at_ms = now;
}
