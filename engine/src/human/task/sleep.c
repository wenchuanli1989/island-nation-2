#include "sleep.h"

#include <stdlib.h>

#include "../behavior/sleep.h"
#include "../needs.h"
#include "../rules.h"
#include "../store.h"
#include "domino_shared_common.h"

/** @brief 空闲时三个槽均为空；运行时仅指定通道包含指定任务，不推断通道与类型的对应关系。 */
static DOMINO_CODE validateActiveSlots(const DominoHumanData* data_ptr, uint8_t channel_index, domino_task_id_t active_task_id) {
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        const domino_task_id_t expected_task_id = channel == channel_index ? active_task_id : 0U;
        if (data_ptr->active_task_id_list[channel] != expected_task_id) {
            return ERR_GAME_STATE_INVALID;
        }
    }
    return CODE_OK;
}

static uint8_t predictEnergy(const DominoHumanData* data_ptr, domino_runtime_ms_t now_ms) {
    const uint32_t minute_count = (now_ms - data_ptr->needs_updated_at_ms) / DOMINO_HUMAN_NEED_MINUTE_MS;
    return dominoHumanDecayNeed(data_ptr->energy, minute_count, DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE);
}

/** @brief 调用方取得有效 store 记录；仅检查本批支持的单行为睡眠形态，不重复扫描任务状态机。 */
static DOMINO_CODE validateSleepTask(const DominoTask* task_ptr, DominoBehavior** behavior_out_ptr) {
    if (!task_ptr) {
        return ERR_NULL_POINTER;
    }
    if (task_ptr->type != DOMINO_TASK_TYPE_SLEEP || task_ptr->behavior_count != 1U) {
        return ERR_GAME_STATE_INVALID;
    }
    const domino_behavior_id_t behavior_id =
        task_ptr->status == DOMINO_TASK_STATUS_SUCCEEDED ? task_ptr->behavior_id_list[0] : task_ptr->current_behavior_id;
    DominoBehavior* behavior_ptr = dominoGetBehaviorByID(behavior_id);
    if (behavior_ptr->type != DOMINO_BEHAVIOR_TYPE_SLEEP) {
        return ERR_GAME_STATE_INVALID;
    }
    *behavior_out_ptr = behavior_ptr;
    return CODE_OK;
}

/** @brief 值输入和绝对到期时刻已通过预检；分配独占行为块并构造有效 PLANNED 草案。 */
static void buildSleepTaskDraft(domino_human_id_t human_id, uint8_t channel_index, domino_runtime_ms_t target_active_ms,
                                domino_runtime_ms_t snapshot_ms, DominoTaskDraft* draft_out_ptr) {
    DominoBehavior* behavior_ptr = calloc(1U, sizeof(*behavior_ptr));
    if (!behavior_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Sleep behavior allocation failed: human=%u bytes=%zu", human_id, sizeof(*behavior_ptr));
    }
    *behavior_ptr = (DominoBehavior){
        .planned_begin_ms = snapshot_ms,
        .planned_end_ms = snapshot_ms + target_active_ms,
        .type = DOMINO_BEHAVIOR_TYPE_SLEEP,
        .status = DOMINO_BEHAVIOR_STATUS_PENDING,
    };
    *draft_out_ptr = (DominoTaskDraft){
        .behaviors_ptr = behavior_ptr,
        .task =
            {
                .owner_human_id = human_id,
                .planned_begin_ms = snapshot_ms,
                .planned_end_ms = snapshot_ms + target_active_ms,
                .type = DOMINO_TASK_TYPE_SLEEP,
                .channel_index = channel_index,
                .behavior_count = 1U,
                .status = DOMINO_TASK_STATUS_PLANNED,
                .priority_class = DOMINO_TASK_PRIORITY_FREE,
            },
    };
}

DOMINO_CODE dominoHumanBuildSleepTaskDraft(domino_human_id_t human_id, uint8_t channel_index, uint8_t energy, domino_runtime_ms_t snapshot_ms,
                                           DominoTaskDraft* draft_out_ptr) {
    if (!draft_out_ptr) {
        return ERR_NULL_POINTER;
    }
    if (human_id == 0U || channel_index >= DOMINO_HUMAN_TASK_CHANNEL_COUNT) {
        return ERR_INVALID_PARAM;
    }
    if (energy > DOMINO_HUMAN_NEED_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    if (energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        return ERR_GAME_STATE_INVALID;
    }
    const domino_runtime_ms_t target_active_ms = dominoHumanEstimateSleepMs(energy);
    if (target_active_ms > UINT32_MAX - snapshot_ms) {
        return ERR_OUT_OF_RANGE;
    }
    buildSleepTaskDraft(human_id, channel_index, target_active_ms, snapshot_ms, draft_out_ptr);
    return CODE_OK;
}

/** @brief 对当前人物预检一次提交资格，并输出此刻的有效睡眠时长；调用方持有同次写入权。 */
static DOMINO_CODE validateSleepSubmission(const DominoHumanData* data_ptr, domino_runtime_ms_t now_ms, domino_runtime_ms_t* target_active_out_ptr) {
    DOMINO_CODE code = dominoHumanValidateNeedSettlement(data_ptr, now_ms);
    if (code != CODE_OK) {
        return code;
    }
    code = validateActiveSlots(data_ptr, DOMINO_HUMAN_TASK_CHANNEL_PRIMARY, 0U);
    if (code != CODE_OK) {
        return code;
    }
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        const DominoTask* existing_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (existing_ptr->type == DOMINO_TASK_TYPE_SLEEP && existing_ptr->status < DOMINO_TASK_STATUS_SUCCEEDED) {
            return ERR_ALREADY_EXISTS;
        }
    }
    const uint8_t energy = predictEnergy(data_ptr, now_ms);
    if (energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        return ERR_GAME_STATE_INVALID;
    }
    const domino_runtime_ms_t target_active_ms = dominoHumanEstimateSleepMs(energy);
    if (target_active_ms > UINT32_MAX - now_ms) {
        return ERR_OUT_OF_RANGE;
    }

    *target_active_out_ptr = target_active_ms;
    return CODE_OK;
}

DOMINO_CODE dominoHumanSubmitSleepTask(DominoHuman* human_ptr, uint8_t channel_index, domino_runtime_ms_t now_ms, domino_task_id_t* task_id_out_ptr) {
    if (!human_ptr || !human_ptr->data || !task_id_out_ptr) {
        return ERR_NULL_POINTER;
    }
    if (human_ptr->status != DOMINO_HUMAN_STATUS_NORMAL) {
        return ERR_GAME_STATE_INVALID;
    }
    DominoHumanData* data_ptr = human_ptr->data;
    if (channel_index >= DOMINO_HUMAN_TASK_CHANNEL_COUNT) {
        return ERR_INVALID_PARAM;
    }
    domino_runtime_ms_t target_active_ms;
    DOMINO_CODE code = validateSleepSubmission(data_ptr, now_ms, &target_active_ms);
    if (code != CODE_OK) {
        return code;
    }
    DominoTaskDraft draft;
    buildSleepTaskDraft(data_ptr->human_id, channel_index, target_active_ms, now_ms, &draft);
    code = dominoHumanStoreAddValidatedTask(&draft.task, draft.behaviors_ptr, data_ptr, task_id_out_ptr);
    free(draft.behaviors_ptr);
    return code;
}

DOMINO_CODE dominoHumanStartSleepTask(DominoTask* task_ptr, DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    if (!human_ptr || !human_ptr->data) {
        return ERR_NULL_POINTER;
    }
    if (human_ptr->status != DOMINO_HUMAN_STATUS_NORMAL) {
        return ERR_GAME_STATE_INVALID;
    }
    DominoHumanData* data_ptr = human_ptr->data;
    DominoBehavior* behavior_ptr;
    DOMINO_CODE code = validateSleepTask(task_ptr, &behavior_ptr);
    if (code != CODE_OK) {
        return code;
    }
    if (now_ms < task_ptr->planned_begin_ms || now_ms < behavior_ptr->planned_begin_ms) {
        return ERR_GAME_STATE_INVALID;
    }
    if (task_ptr->status != DOMINO_TASK_STATUS_PLANNED) {
        return ERR_GAME_STATE_INVALID;
    }
    code = validateActiveSlots(data_ptr, task_ptr->channel_index, 0U);
    if (code != CODE_OK) {
        return code;
    }
    if (now_ms < data_ptr->needs_updated_at_ms) {
        return ERR_INVALID_PARAM;
    }
    const uint8_t energy = predictEnergy(data_ptr, now_ms);
    if (energy >= DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY) {
        return ERR_GAME_STATE_INVALID;
    }
    const domino_runtime_ms_t target_active_ms = dominoHumanEstimateSleepMs(energy);
    if (target_active_ms > UINT32_MAX - now_ms) {
        return ERR_OUT_OF_RANGE;
    }
    const domino_runtime_ms_t planned_end_ms = now_ms + target_active_ms;
    const bool plan_changed_flag = task_ptr->planned_begin_ms != now_ms || task_ptr->planned_end_ms != planned_end_ms ||
                                   behavior_ptr->planned_begin_ms != now_ms || behavior_ptr->planned_end_ms != planned_end_ms;
    if (plan_changed_flag && data_ptr->schedule_revision == UINT32_MAX) {
        return ERR_OUT_OF_RANGE;
    }

    /* 所有可失败步骤均已完成，按当前需求启动独立任务。 */
    dominoHumanApplyNaturalNeeds(data_ptr, now_ms);
    if (plan_changed_flag) {
        task_ptr->planned_begin_ms = now_ms;
        task_ptr->planned_end_ms = planned_end_ms;
        behavior_ptr->planned_begin_ms = now_ms;
        behavior_ptr->planned_end_ms = planned_end_ms;
        data_ptr->schedule_revision++;
    }
    task_ptr->status = DOMINO_TASK_STATUS_RUNNING;
    task_ptr->started_flag = true;
    task_ptr->started_at_ms = now_ms;
    behavior_ptr->started_at_ms = now_ms;
    behavior_ptr->ended_at_ms = 0U;
    behavior_ptr->accounted_at_ms = now_ms;
    behavior_ptr->active_elapsed_ms = 0U;
    behavior_ptr->status = DOMINO_BEHAVIOR_STATUS_ACTIVE;
    behavior_ptr->started_flag = true;
    data_ptr->active_task_id_list[task_ptr->channel_index] = task_ptr->task_id;
    return CODE_OK;
}

DOMINO_CODE dominoHumanAdvanceSleepTask(DominoTask* task_ptr, DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    if (!human_ptr || !human_ptr->data) {
        return ERR_NULL_POINTER;
    }
    if (human_ptr->status != DOMINO_HUMAN_STATUS_NORMAL) {
        return ERR_GAME_STATE_INVALID;
    }
    DominoHumanData* data_ptr = human_ptr->data;
    DominoBehavior* behavior_ptr;
    DOMINO_CODE code = validateSleepTask(task_ptr, &behavior_ptr);
    if (code != CODE_OK) {
        return code;
    }
    if (task_ptr->status >= DOMINO_TASK_STATUS_SUCCEEDED) {
        if (now_ms < task_ptr->ended_at_ms) {
            return ERR_INVALID_PARAM;
        }
        if (task_ptr->status == DOMINO_TASK_STATUS_SUCCEEDED && behavior_ptr->status != DOMINO_BEHAVIOR_STATUS_DONE) {
            return ERR_INVALID_DATA;
        }
        return CODE_OK;
    }
    if (task_ptr->status != DOMINO_TASK_STATUS_RUNNING || behavior_ptr->status != DOMINO_BEHAVIOR_STATUS_ACTIVE) {
        return ERR_GAME_STATE_INVALID;
    }
    code = validateActiveSlots(data_ptr, task_ptr->channel_index, task_ptr->task_id);
    if (code != CODE_OK) {
        return code;
    }
    if (now_ms < behavior_ptr->accounted_at_ms) {
        return ERR_INVALID_PARAM;
    }
    if (data_ptr->needs_updated_at_ms > behavior_ptr->accounted_at_ms ||
        behavior_ptr->accounted_at_ms - data_ptr->needs_updated_at_ms >= DOMINO_HUMAN_NEED_MINUTE_MS) {
        return ERR_INVALID_DATA;
    }
    if (behavior_ptr->planned_end_ms < behavior_ptr->planned_begin_ms) {
        return ERR_INVALID_DATA;
    }
    const domino_runtime_ms_t target_active_ms = behavior_ptr->planned_end_ms - behavior_ptr->planned_begin_ms;
    if (behavior_ptr->active_elapsed_ms > target_active_ms) {
        return ERR_INVALID_DATA;
    }
    const domino_runtime_ms_t remaining_ms = target_active_ms - behavior_ptr->active_elapsed_ms;
    const bool completing_flag = now_ms - behavior_ptr->accounted_at_ms >= remaining_ms;
    if (completing_flag && task_ptr->execution_version == UINT64_MAX) {
        return ERR_OUT_OF_RANGE;
    }

    dominoHumanApplySleep(data_ptr, behavior_ptr, now_ms);
    if (completing_flag) {
        task_ptr->status = DOMINO_TASK_STATUS_SUCCEEDED;
        task_ptr->current_behavior_id = 0U;
        task_ptr->ended_at_ms = now_ms;
        task_ptr->execution_version++;
        data_ptr->active_task_id_list[task_ptr->channel_index] = 0U;
    }
    return CODE_OK;
}
