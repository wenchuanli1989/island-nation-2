#include "sleep.h"

#include <stdlib.h>

#include "../behavior/sleep.h"
#include "../rules.h"
#include "../state.h"
#include "domino_shared_common.h"

/** @brief 值输入和绝对到期时刻已通过预检；分配独占行为块并构造有效 PLANNED 草案。 */
static void buildSleepTaskDraft(domino_human_id_t human_id, domino_runtime_ms_t snapshot_ms, domino_runtime_ms_t planned_end_ms,
                                DominoTaskDraft* draft_out_ptr) {
    DominoBehavior* behavior_ptr = malloc(sizeof(*behavior_ptr));
    if (!behavior_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Sleep behavior allocation failed: human=%u bytes=%zu", human_id, sizeof(*behavior_ptr));
    }
    *behavior_ptr = (DominoBehavior){
        .planned_begin_ms = snapshot_ms,
        .planned_end_ms = planned_end_ms,
        .type = DOMINO_BEHAVIOR_TYPE_SLEEP,
        .status = DOMINO_BEHAVIOR_STATUS_PENDING,
    };
    *draft_out_ptr = (DominoTaskDraft){
        .behaviors_ptr = behavior_ptr,
        .owner_human_id = human_id,
        .planned_begin_ms = snapshot_ms,
        .planned_end_ms = planned_end_ms,
        .type = DOMINO_TASK_TYPE_SLEEP,
        .channel_index = DOMINO_HUMAN_TASK_CHANNEL_PRIMARY,
        .behavior_count = 1U,
        .priority_class = DOMINO_TASK_PRIORITY_FREE,
    };
}

DOMINO_CODE dominoHumanBuildSleepTaskDraft(domino_human_id_t human_id, uint8_t energy, domino_runtime_ms_t snapshot_ms,
                                           DominoTaskDraft* draft_out_ptr) {
    if (!draft_out_ptr) {
        return ERR_NULL_POINTER;
    }
    if (human_id == 0U) {
        return ERR_INVALID_PARAM;
    }
    if (energy > DOMINO_HUMAN_NEED_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    if (energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        return ERR_GAME_STATE_INVALID;
    }
    domino_runtime_ms_t planned_end_ms;
    const DOMINO_CODE code = dominoHumanPrepareSleep(energy, snapshot_ms, &planned_end_ms);
    if (code != CODE_OK) {
        return code;
    }
    buildSleepTaskDraft(human_id, snapshot_ms, planned_end_ms, draft_out_ptr);
    return CODE_OK;
}

/** @brief 执行器已确认任务形态、通道和开始时刻，并共同结算了当前需求；这里只处理睡眠启动条件。 */
static DOMINO_CODE startSleepTask(DominoTask* task_ptr, DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    domino_runtime_ms_t planned_end_ms;
    const DOMINO_CODE code = dominoHumanPrepareSleep(human_ptr->data->energy, now_ms, &planned_end_ms);
    if (code != CODE_OK) {
        return code;
    }
    /* 所有可失败步骤均已完成，按当前需求启动独立任务。 */
    DominoBehavior* behavior_ptr = dominoGetBehaviorByID(task_ptr->current_behavior_id);
    task_ptr->planned_begin_ms = now_ms;
    task_ptr->planned_end_ms = planned_end_ms;
    behavior_ptr->planned_begin_ms = now_ms;
    behavior_ptr->planned_end_ms = planned_end_ms;
    behavior_ptr->started_at_ms = now_ms;
    behavior_ptr->ended_at_ms = 0U;
    behavior_ptr->accounted_at_ms = now_ms;
    behavior_ptr->active_elapsed_ms = 0U;
    behavior_ptr->status = DOMINO_BEHAVIOR_STATUS_ACTIVE;
    behavior_ptr->started_flag = true;
    return CODE_OK;
}

/** @brief 在执行器选定的效果边界推进本任务；活动槽和自然需求均由执行器统一管理。 */
static DOMINO_CODE advanceSleepTask(DominoTask* task_ptr, DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    DominoBehavior* behavior_ptr = dominoGetBehaviorByID(task_ptr->current_behavior_id);
    dominoHumanApplySleep(human_ptr->data, behavior_ptr, now_ms);
    if (behavior_ptr->status == DOMINO_BEHAVIOR_STATUS_DONE) {
        task_ptr->status = DOMINO_TASK_STATUS_SUCCEEDED;
        task_ptr->current_behavior_id = 0U;
        task_ptr->ended_at_ms = now_ms;
    }
    return CODE_OK;
}

/** @brief 当前睡眠实现只接受单个睡眠行为，其他合法形态由后续业务实现接入。 */
static bool supportsSleepTask(const DominoTask* task_ptr) {
    if (task_ptr->behavior_count != 1U) {
        return false;
    }
    return dominoGetBehaviorByID(task_ptr->behavior_id_list[0])->type == DOMINO_BEHAVIOR_TYPE_SLEEP;
}

/** @brief 内部效果每恢复一点精力结算，外部只需在剩余有效睡眠结束时唤醒。 */
static void getSleepTaskTimes(const DominoTask* task_ptr, const DominoBehavior* behavior_ptr, domino_runtime_ms_t* settle_ms_ptr,
                              domino_runtime_ms_t* wakeup_ms_ptr) {
    const domino_runtime_ms_t remaining_ms = dominoHumanGetRemainingSleepMs(behavior_ptr);
    if (remaining_ms > UINT32_MAX - behavior_ptr->accounted_at_ms) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "sleep deadline overflows for task=%u", task_ptr->task_id);
    }
    const domino_runtime_ms_t reward_ms = DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS - behavior_ptr->active_elapsed_ms % DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS;
    *settle_ms_ptr = behavior_ptr->accounted_at_ms + (reward_ms < remaining_ms ? reward_ms : remaining_ms);
    *wakeup_ms_ptr = behavior_ptr->accounted_at_ms + remaining_ms;
}

const DominoTaskOps domino_sleep_task_ops = {
    .supports = supportsSleepTask,
    .start = startSleepTask,
    .advance = advanceSleepTask,
    .get_times = getSleepTaskTimes,
};
