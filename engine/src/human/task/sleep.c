#include "sleep.h"

#include <stdlib.h>

#include "../../logger/entry.h"
#include "../../social/entry.h"
#include "../behavior/sleep.h"
#include "../needs.h"
#include "../rules.h"
#include "../store.h"
#include "entry.h"

static DOMINO_CODE validateSleepBuilding(const DominoHumanData* data_ptr, domino_building_id_t approved_sleep_building_id) {
    if (approved_sleep_building_id == 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep building approval is zero: human=%u", data_ptr->human_id);
        return ERR_INVALID_PARAM;
    }
    if (!dominoGetBuildingByID(approved_sleep_building_id)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "Sleep building no longer exists: human=%u building=%u",
                          data_ptr->human_id, approved_sleep_building_id);
        return ERR_NOT_FOUND;
    }
    if (data_ptr->current_building_id != approved_sleep_building_id) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "Sleep building changed: human=%u current=%u approved=%u",
                          data_ptr->human_id, data_ptr->current_building_id, approved_sleep_building_id);
        return ERR_GAME_STATE_INVALID;
    }
    return CODE_OK;
}

/** @brief 空闲时三个槽均为空；运行时仅指定通道包含指定任务，不推断通道与类型的对应关系。 */
static DOMINO_CODE validateActiveSlots(const DominoHumanData* data_ptr, uint8_t channel_index, domino_task_id_t active_task_id) {
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        const domino_task_id_t expected_task_id = channel == channel_index ? active_task_id : 0U;
        if (data_ptr->active_task_id_list[channel] != expected_task_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                              "Sleep active slot changed: human=%u channel=%u task=%u expected=%u", data_ptr->human_id, channel,
                              data_ptr->active_task_id_list[channel], expected_task_id);
            return ERR_GAME_STATE_INVALID;
        }
    }
    return CODE_OK;
}

static uint8_t predictEnergy(const DominoHumanData* data_ptr, domino_runtime_ms_t now_ms) {
    const uint32_t minute_count = (now_ms - data_ptr->needs_updated_at_ms) / DOMINO_HUMAN_NEED_MINUTE_MS;
    return dominoHumanDecayNeed(data_ptr->energy, minute_count, DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE);
}

/** @brief 检查本批可执行的记录形态；调用方已取得任务及所属人物，清单关系由加载校验和 store 入口保证。 */
static DOMINO_CODE validateSleepTask(const DominoTask* task_ptr) {
    DOMINO_CODE code = dominoHumanValidateTaskRecord(task_ptr);
    if (code != CODE_OK) {
        return code;
    }
    if (task_ptr->type != DOMINO_TASK_TYPE_SLEEP || task_ptr->behavior_count != 1U || task_ptr->behaviors_ptr[0].type != DOMINO_BEHAVIOR_TYPE_SLEEP ||
        task_ptr->parent_task_id || task_ptr->active_child_task_id || task_ptr->pending_stop_reason != DOMINO_TASK_STOP_NONE) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Unsupported sleep task shape: task=%u type=%u behaviors=%u parent=%u child=%u stop_reason=%u", task_ptr->task_id,
                          task_ptr->type, task_ptr->behavior_count, task_ptr->parent_task_id, task_ptr->active_child_task_id,
                          task_ptr->pending_stop_reason);
        return ERR_GAME_STATE_INVALID;
    }
    return CODE_OK;
}

/** @brief 值输入和绝对到期时刻已通过预检；分配独占行为块并构造无关系的有效 PLANNED 草案。 */
static DOMINO_CODE buildSleepTaskDraft(domino_human_id_t human_id, domino_building_id_t approved_sleep_building_id, uint8_t channel_index,
                                       domino_runtime_ms_t target_active_ms, domino_runtime_ms_t snapshot_ms, DominoTask* draft_out_ptr) {
    DominoBehavior* behavior_ptr = calloc(1U, sizeof(*behavior_ptr));
    if (!behavior_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep behavior allocation failed: human=%u bytes=%zu", human_id,
                          sizeof(*behavior_ptr));
        return ERR_MEMORY_ALLOC;
    }
    *behavior_ptr = (DominoBehavior){
        .progress.status = DOMINO_BEHAVIOR_STATUS_PENDING,
        .parameters.sleep.target_active_ms = target_active_ms,
        .target_building_id = approved_sleep_building_id,
        .planned_begin_ms = snapshot_ms,
        .planned_end_ms = snapshot_ms + target_active_ms,
        .type = DOMINO_BEHAVIOR_TYPE_SLEEP,
    };
    *draft_out_ptr = (DominoTask){
        .behaviors_ptr = behavior_ptr,
        .owner_human_id = human_id,
        .planned_begin_ms = snapshot_ms,
        .planned_end_ms = snapshot_ms + target_active_ms,
        .type = DOMINO_TASK_TYPE_SLEEP,
        .channel_index = channel_index,
        .behavior_count = 1U,
        .status = DOMINO_TASK_STATUS_PLANNED,
        .priority_class = DOMINO_TASK_PRIORITY_FREE,
    };
    return CODE_OK;
}

DOMINO_CODE dominoHumanBuildSleepTaskDraft(domino_human_id_t human_id, domino_building_id_t approved_sleep_building_id, uint8_t channel_index,
                                           uint8_t energy, domino_runtime_ms_t snapshot_ms, DominoTask* draft_out_ptr) {
    if (!draft_out_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep draft output is null: human=%u", human_id);
        return ERR_NULL_POINTER;
    }
    if (human_id == 0U || approved_sleep_building_id == 0U || channel_index >= DOMINO_HUMAN_TASK_CHANNEL_COUNT) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Invalid sleep draft input: human=%u building=%u channel=%u",
                          human_id, approved_sleep_building_id, channel_index);
        return ERR_INVALID_PARAM;
    }
    if (energy > DOMINO_HUMAN_NEED_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep draft energy exceeds range: human=%u energy=%u", human_id,
                          energy);
        return ERR_OUT_OF_RANGE;
    }
    if (energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "Sleep draft rejected by energy: human=%u energy=%u", human_id,
                          energy);
        return ERR_GAME_STATE_INVALID;
    }
    const domino_runtime_ms_t target_active_ms = dominoHumanEstimateSleepMs(energy);
    if (target_active_ms > UINT32_MAX - snapshot_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep draft end time exceeds runtime range: human=%u snapshot_ms=%u target_active_ms=%u", human_id, snapshot_ms,
                          target_active_ms);
        return ERR_OUT_OF_RANGE;
    }
    return buildSleepTaskDraft(human_id, approved_sleep_building_id, channel_index, target_active_ms, snapshot_ms, draft_out_ptr);
}

/** @brief 对当前人物预检一次提交资格，并输出此刻的有效睡眠时长；调用方持有同次写入权。 */
static DOMINO_CODE validateSleepSubmission(const DominoHumanData* data_ptr, domino_building_id_t approved_sleep_building_id,
                                           domino_runtime_ms_t now_ms, domino_runtime_ms_t* target_active_out_ptr) {
    DOMINO_CODE code = validateSleepBuilding(data_ptr, approved_sleep_building_id);
    if (code != CODE_OK) {
        return code;
    }
    code = dominoHumanValidateNeedSettlement(data_ptr, now_ms);
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
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "Unfinished sleep task already exists: human=%u task=%u",
                              data_ptr->human_id, existing_ptr->task_id);
            return ERR_ALREADY_EXISTS;
        }
    }
    const uint8_t energy = predictEnergy(data_ptr, now_ms);
    if (energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "Sleep submission rejected by energy: human=%u energy=%u",
                          data_ptr->human_id, energy);
        return ERR_GAME_STATE_INVALID;
    }
    const domino_runtime_ms_t target_active_ms = dominoHumanEstimateSleepMs(energy);
    if (target_active_ms > UINT32_MAX - now_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep submission end time exceeds runtime range: human=%u now_ms=%u target_active_ms=%u", data_ptr->human_id, now_ms,
                          target_active_ms);
        return ERR_OUT_OF_RANGE;
    }

    *target_active_out_ptr = target_active_ms;
    return CODE_OK;
}

/** @brief 草案及当前资格已预检；store 成功后立即交出行为块所有权，不再执行可失败步骤。 */
static DOMINO_CODE commitSleepTaskDraft(DominoTask* draft_ptr, DominoHumanData* data_ptr, domino_task_id_t* task_id_out_ptr) {
    const DOMINO_CODE code = dominoHumanStoreAddValidatedTask(draft_ptr, data_ptr, task_id_out_ptr);
    if (code == CODE_OK) {
        draft_ptr->behaviors_ptr = nullptr;
    }
    return code;
}

DOMINO_CODE dominoHumanCommitSleepTaskDraft(DominoTask* draft_ptr, DominoHumanData* data_ptr, domino_building_id_t approved_sleep_building_id,
                                            uint32_t expected_schedule_revision, domino_runtime_ms_t now_ms, domino_task_id_t* task_id_out_ptr) {
    if (!draft_ptr || !data_ptr || !task_id_out_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep draft commit requires draft, human data and task output");
        return ERR_NULL_POINTER;
    }
    DOMINO_CODE code = dominoHumanValidateTaskRecord(draft_ptr);
    if (code != CODE_OK) {
        return code;
    }
    if (draft_ptr->task_id != 0U || draft_ptr->type != DOMINO_TASK_TYPE_SLEEP || draft_ptr->status != DOMINO_TASK_STATUS_PLANNED ||
        draft_ptr->behavior_count != 1U || draft_ptr->behaviors_ptr[0].type != DOMINO_BEHAVIOR_TYPE_SLEEP || draft_ptr->parent_task_id != 0U ||
        draft_ptr->active_child_task_id != 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep commit requires an unsubmitted independent planned sleep draft: human=%u task=%u type=%u status=%u",
                          data_ptr->human_id, draft_ptr->task_id, draft_ptr->type, draft_ptr->status);
        return ERR_INVALID_PARAM;
    }
    const DominoBehavior* behavior_ptr = &draft_ptr->behaviors_ptr[0];
    if (now_ms < draft_ptr->planned_begin_ms || now_ms < behavior_ptr->planned_begin_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep commit precedes draft time: human=%u now_ms=%u task_begin_ms=%u behavior_begin_ms=%u", data_ptr->human_id, now_ms,
                          draft_ptr->planned_begin_ms, behavior_ptr->planned_begin_ms);
        return ERR_INVALID_PARAM;
    }
    if (behavior_ptr->target_building_id != approved_sleep_building_id || data_ptr->schedule_revision != expected_schedule_revision) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                          "Sleep draft facts changed: human=%u target=%u approved=%u revision=%u expected=%u", data_ptr->human_id,
                          behavior_ptr->target_building_id, approved_sleep_building_id, data_ptr->schedule_revision, expected_schedule_revision);
        return ERR_GAME_STATE_INVALID;
    }
    domino_runtime_ms_t target_active_ms;
    code = validateSleepSubmission(data_ptr, approved_sleep_building_id, now_ms, &target_active_ms);
    if (code != CODE_OK) {
        return code;
    }
    return commitSleepTaskDraft(draft_ptr, data_ptr, task_id_out_ptr);
}

DOMINO_CODE dominoHumanSubmitSleepTask(DominoHumanData* data_ptr, domino_building_id_t approved_sleep_building_id, uint8_t channel_index,
                                       domino_runtime_ms_t now_ms, domino_task_id_t* task_id_out_ptr) {
    if (!data_ptr || !task_id_out_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep submission requires human data and task output");
        return ERR_NULL_POINTER;
    }
    if (channel_index >= DOMINO_HUMAN_TASK_CHANNEL_COUNT) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Invalid sleep submission channel: human=%u channel=%u",
                          data_ptr->human_id, channel_index);
        return ERR_INVALID_PARAM;
    }
    domino_runtime_ms_t target_active_ms;
    DOMINO_CODE code = validateSleepSubmission(data_ptr, approved_sleep_building_id, now_ms, &target_active_ms);
    if (code != CODE_OK) {
        return code;
    }
    DominoTask draft = {0};
    code = buildSleepTaskDraft(data_ptr->human_id, approved_sleep_building_id, channel_index, target_active_ms, now_ms, &draft);
    if (code != CODE_OK) {
        return code;
    }
    code = commitSleepTaskDraft(&draft, data_ptr, task_id_out_ptr);
    free(draft.behaviors_ptr);
    return code;
}

DOMINO_CODE dominoHumanStartSleepTask(DominoTask* task_ptr, DominoHumanData* data_ptr, domino_building_id_t approved_sleep_building_id,
                                      domino_runtime_ms_t now_ms) {
    if (!data_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep start human data is null");
        return ERR_NULL_POINTER;
    }
    DOMINO_CODE code = validateSleepTask(task_ptr);
    if (code != CODE_OK) {
        return code;
    }
    DominoBehavior* behavior_ptr = &task_ptr->behaviors_ptr[0];
    if (now_ms < task_ptr->planned_begin_ms || now_ms < behavior_ptr->planned_begin_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                          "Sleep start precedes planned time: task=%u now_ms=%u task_begin_ms=%u behavior_begin_ms=%u", task_ptr->task_id, now_ms,
                          task_ptr->planned_begin_ms, behavior_ptr->planned_begin_ms);
        return ERR_GAME_STATE_INVALID;
    }
    if (task_ptr->status != DOMINO_TASK_STATUS_PLANNED || behavior_ptr->target_building_id != approved_sleep_building_id) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep start state or approval mismatch: task=%u status=%u target=%u approved=%u", task_ptr->task_id, task_ptr->status,
                          behavior_ptr->target_building_id, approved_sleep_building_id);
        return ERR_GAME_STATE_INVALID;
    }
    code = validateActiveSlots(data_ptr, task_ptr->channel_index, 0U);
    if (code != CODE_OK) {
        return code;
    }
    code = validateSleepBuilding(data_ptr, approved_sleep_building_id);
    if (code != CODE_OK) {
        return code;
    }
    code = dominoHumanValidateNeedSettlement(data_ptr, now_ms);
    if (code != CODE_OK) {
        return code;
    }
    const uint8_t energy = predictEnergy(data_ptr, now_ms);
    if (energy >= DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "Sleep start rejected by satisfied energy: human=%u energy=%u",
                          data_ptr->human_id, energy);
        return ERR_GAME_STATE_INVALID;
    }
    const domino_runtime_ms_t target_active_ms = dominoHumanEstimateSleepMs(energy);
    if (target_active_ms > UINT32_MAX - now_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep start end time exceeds runtime range: task=%u now_ms=%u target_active_ms=%u", task_ptr->task_id, now_ms,
                          target_active_ms);
        return ERR_OUT_OF_RANGE;
    }
    const domino_runtime_ms_t planned_end_ms = now_ms + target_active_ms;
    const bool plan_changed_flag = task_ptr->planned_begin_ms != now_ms || task_ptr->planned_end_ms != planned_end_ms ||
                                   behavior_ptr->planned_begin_ms != now_ms || behavior_ptr->planned_end_ms != planned_end_ms ||
                                   behavior_ptr->parameters.sleep.target_active_ms != target_active_ms;
    if (plan_changed_flag && data_ptr->schedule_revision == UINT32_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep start schedule revision exhausted: human=%u task=%u",
                          data_ptr->human_id, task_ptr->task_id);
        return ERR_OUT_OF_RANGE;
    }

    /* 所有可失败步骤均已完成；有效 store 中待交接子任务有 RUNNING 父任务，三槽全空已排除被独立启动。 */
    dominoHumanApplyNaturalNeeds(data_ptr, now_ms);
    if (plan_changed_flag) {
        task_ptr->planned_begin_ms = now_ms;
        task_ptr->planned_end_ms = planned_end_ms;
        behavior_ptr->planned_begin_ms = now_ms;
        behavior_ptr->planned_end_ms = planned_end_ms;
        behavior_ptr->parameters.sleep.target_active_ms = target_active_ms;
        data_ptr->schedule_revision++;
    }
    task_ptr->status = DOMINO_TASK_STATUS_RUNNING;
    task_ptr->started_flag = true;
    task_ptr->started_at_ms = now_ms;
    behavior_ptr->progress = (DominoBehaviorProgress){
        .started_at_ms = now_ms,
        .accounted_at_ms = now_ms,
        .status = DOMINO_BEHAVIOR_STATUS_ACTIVE,
        .started_flag = true,
    };
    data_ptr->active_task_id_list[task_ptr->channel_index] = task_ptr->task_id;
    return CODE_OK;
}

DOMINO_CODE dominoHumanAdvanceSleepTask(DominoTask* task_ptr, DominoHumanData* data_ptr, domino_building_id_t approved_sleep_building_id,
                                        domino_runtime_ms_t now_ms) {
    if (!data_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep advance human data is null");
        return ERR_NULL_POINTER;
    }
    DOMINO_CODE code = validateSleepTask(task_ptr);
    if (code != CODE_OK) {
        return code;
    }
    DominoBehavior* behavior_ptr = &task_ptr->behaviors_ptr[0];
    DominoBehaviorProgress* progress_ptr = &behavior_ptr->progress;
    if (task_ptr->status >= DOMINO_TASK_STATUS_SUCCEEDED) {
        if (now_ms < task_ptr->ended_at_ms) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "Sleep advance precedes task end: task=%u now_ms=%u ended_at_ms=%u", task_ptr->task_id, now_ms, task_ptr->ended_at_ms);
            return ERR_INVALID_PARAM;
        }
        if (task_ptr->status == DOMINO_TASK_STATUS_SUCCEEDED && progress_ptr->status != DOMINO_BEHAVIOR_STATUS_DONE) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "Completed sleep task has unfinished behavior: task=%u behavior_status=%u", task_ptr->task_id, progress_ptr->status);
            return ERR_INVALID_DATA;
        }
        return CODE_OK;
    }
    if (task_ptr->status != DOMINO_TASK_STATUS_RUNNING || progress_ptr->status != DOMINO_BEHAVIOR_STATUS_ACTIVE ||
        behavior_ptr->target_building_id != approved_sleep_building_id) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep advance state or approval mismatch: task=%u status=%u behavior_status=%u target=%u approved=%u", task_ptr->task_id,
                          task_ptr->status, progress_ptr->status, behavior_ptr->target_building_id, approved_sleep_building_id);
        return ERR_GAME_STATE_INVALID;
    }
    code = validateActiveSlots(data_ptr, task_ptr->channel_index, task_ptr->task_id);
    if (code != CODE_OK) {
        return code;
    }
    code = validateSleepBuilding(data_ptr, approved_sleep_building_id);
    if (code != CODE_OK) {
        return code;
    }
    code = dominoHumanValidateNeedSettlement(data_ptr, now_ms);
    if (code != CODE_OK) {
        return code;
    }
    if (now_ms < progress_ptr->accounted_at_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep advance precedes accounted time: task=%u now_ms=%u accounted_at_ms=%u", task_ptr->task_id, now_ms,
                          progress_ptr->accounted_at_ms);
        return ERR_INVALID_PARAM;
    }
    if (data_ptr->needs_updated_at_ms > progress_ptr->accounted_at_ms ||
        progress_ptr->accounted_at_ms - data_ptr->needs_updated_at_ms >= DOMINO_HUMAN_NEED_MINUTE_MS) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Sleep and needs accounting disagree: human=%u task=%u needs_updated_at_ms=%u accounted_at_ms=%u", data_ptr->human_id,
                          task_ptr->task_id, data_ptr->needs_updated_at_ms, progress_ptr->accounted_at_ms);
        return ERR_INVALID_DATA;
    }
    const domino_runtime_ms_t remaining_ms = behavior_ptr->parameters.sleep.target_active_ms - progress_ptr->active_elapsed_ms;
    const bool completing_flag = now_ms - progress_ptr->accounted_at_ms >= remaining_ms;
    if (completing_flag && task_ptr->execution_version == UINT64_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Sleep completion execution version exhausted: task=%u",
                          task_ptr->task_id);
        return ERR_OUT_OF_RANGE;
    }

    dominoHumanApplySleep(data_ptr, behavior_ptr, now_ms);
    if (completing_flag) {
        task_ptr->status = DOMINO_TASK_STATUS_SUCCEEDED;
        task_ptr->result = DOMINO_TASK_RESULT_COMPLETED;
        task_ptr->current_behavior_index = task_ptr->behavior_count;
        task_ptr->ended_at_ms = now_ms;
        task_ptr->execution_version++;
        data_ptr->active_task_id_list[task_ptr->channel_index] = 0U;
    }
    return CODE_OK;
}
