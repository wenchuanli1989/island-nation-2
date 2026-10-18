#include "sleep.h"

#include "../needs.h"
#include "../rules.h"

void dominoHumanApplySleep(DominoHumanData* data_ptr, DominoBehavior* behavior_ptr, domino_runtime_ms_t now_ms) {
    DominoBehaviorProgress* progress_ptr = &behavior_ptr->progress;
    const domino_runtime_ms_t old_accounted_ms = progress_ptr->accounted_at_ms;
    const domino_runtime_ms_t target_active_ms = behavior_ptr->parameters.sleep.target_active_ms;
    const domino_runtime_ms_t remaining_ms = target_active_ms - progress_ptr->active_elapsed_ms;
    const domino_runtime_ms_t elapsed_ms = now_ms - old_accounted_ms;
    const domino_runtime_ms_t span_ms = elapsed_ms < remaining_ms ? elapsed_ms : remaining_ms;
    domino_runtime_ms_t settled_span_ms = 0U;
    domino_runtime_ms_t reward_step_ms = DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS - progress_ptr->active_elapsed_ms % DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS;

    while (reward_step_ms <= span_ms - settled_span_ms) {
        /* 先比较剩余区间再相加，收益时刻始终不晚于 now_ms，不会溢出累计毫秒。 */
        settled_span_ms += reward_step_ms;
        dominoHumanApplyNaturalNeeds(data_ptr, old_accounted_ms + settled_span_ms);
        if (data_ptr->energy < DOMINO_HUMAN_NEED_MAX) {
            data_ptr->energy++;
        }
        reward_step_ms = DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS;
    }

    /* 延迟处理跨过睡眠目标时，目标之后的时间仍结算自然衰减，不再发放睡眠收益。 */
    dominoHumanApplyNaturalNeeds(data_ptr, now_ms);
    progress_ptr->active_elapsed_ms += span_ms;
    progress_ptr->accounted_at_ms = now_ms;
    if (progress_ptr->active_elapsed_ms == target_active_ms) {
        progress_ptr->status = DOMINO_BEHAVIOR_STATUS_DONE;
        progress_ptr->ended_at_ms = now_ms;
    }
}
