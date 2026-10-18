#include "entry.h"

#include <inttypes.h>
#include <stddef.h>

#include "../../logger/entry.h"
#include "../behavior/entry.h"

static const DominoTaskTypeInfo g_task_type_info[DOMINO_TASK_TYPE_COUNT] = {
    [DOMINO_TASK_TYPE_WORK] = {.name_str = "work"},
    [DOMINO_TASK_TYPE_SCHOOL] = {.name_str = "school"},
    [DOMINO_TASK_TYPE_ENTERTAINMENT] = {.name_str = "entertainment"},
    [DOMINO_TASK_TYPE_SHOPPING] = {.name_str = "shopping"},
    [DOMINO_TASK_TYPE_EXERCISE] = {.name_str = "exercise"},
    [DOMINO_TASK_TYPE_MEDICAL] = {.name_str = "medical"},
    [DOMINO_TASK_TYPE_VISIT] = {.name_str = "visit"},
    [DOMINO_TASK_TYPE_TRAVEL] = {.name_str = "travel"},
    [DOMINO_TASK_TYPE_PARADE] = {.name_str = "parade"},
    [DOMINO_TASK_TYPE_SLEEP] = {.name_str = "sleep"},
    [DOMINO_TASK_TYPE_PHONE_CALL] = {.name_str = "phone_call"},
    [DOMINO_TASK_TYPE_MESSAGE] = {.name_str = "message"},
};

const DominoTaskTypeInfo* dominoHumanGetTaskTypeInfo(DOMINO_TASK_TYPE type) {
    if (type <= DOMINO_TASK_TYPE_NONE || type >= DOMINO_TASK_TYPE_COUNT) {
        return NULL;
    }
    return &g_task_type_info[type];
}

/** @brief 校验停止原因及意图字段；调用方已检查取消范围，关系目标的存在性由 store 统一检查。 */
static DOMINO_CODE validateStopIntent(const DominoTask* task_ptr) {
    if (task_ptr->pending_stop_reason == DOMINO_TASK_STOP_NONE) {
        if (task_ptr->cancel_scope != DOMINO_TASK_CANCEL_NONE || task_ptr->cancel_target_task_id != 0 || task_ptr->pending_child_task_id != 0) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "Task has stop fields without stop intent: task=%" PRIu32 " scope=%u target=%" PRIu32 " child=%" PRIu32,
                              task_ptr->task_id, task_ptr->cancel_scope, task_ptr->cancel_target_task_id, task_ptr->pending_child_task_id);
            return ERR_INVALID_DATA;
        }
        return CODE_OK;
    }
    if (task_ptr->status != DOMINO_TASK_STATUS_RUNNING) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Non-running task has stop intent: task=%" PRIu32 " status=%u reason=%u", task_ptr->task_id, task_ptr->status,
                          task_ptr->pending_stop_reason);
        return ERR_INVALID_DATA;
    }
    switch (task_ptr->pending_stop_reason) {
        case DOMINO_TASK_STOP_PREEMPT:
            if (task_ptr->pending_child_task_id == 0 || task_ptr->cancel_scope != DOMINO_TASK_CANCEL_NONE || task_ptr->cancel_target_task_id != 0) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Invalid preemption intent: task=%" PRIu32 " child=%" PRIu32 " scope=%u target=%" PRIu32, task_ptr->task_id,
                                  task_ptr->pending_child_task_id, task_ptr->cancel_scope, task_ptr->cancel_target_task_id);
                return ERR_INVALID_DATA;
            }
            return CODE_OK;
        case DOMINO_TASK_STOP_CANCEL:
            if (task_ptr->pending_child_task_id != 0 || task_ptr->cancel_scope == DOMINO_TASK_CANCEL_NONE) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Invalid cancellation intent: task=%" PRIu32 " child=%" PRIu32 " scope=%u", task_ptr->task_id,
                                  task_ptr->pending_child_task_id, task_ptr->cancel_scope);
                return ERR_INVALID_DATA;
            }
            if ((task_ptr->cancel_scope == DOMINO_TASK_CANCEL_ALL) != (task_ptr->cancel_target_task_id == 0)) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Cancellation scope and target disagree: task=%" PRIu32 " scope=%u target=%" PRIu32, task_ptr->task_id,
                                  task_ptr->cancel_scope, task_ptr->cancel_target_task_id);
                return ERR_INVALID_DATA;
            }
            return CODE_OK;
        case DOMINO_TASK_STOP_DANGER:
            if (task_ptr->cancel_scope != DOMINO_TASK_CANCEL_NONE || task_ptr->cancel_target_task_id != 0) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Danger stop has cancellation fields: task=%" PRIu32 " scope=%u target=%" PRIu32, task_ptr->task_id,
                                  task_ptr->cancel_scope, task_ptr->cancel_target_task_id);
                return ERR_INVALID_DATA;
            }
            return CODE_OK;
        case DOMINO_TASK_STOP_DEATH:
            if (task_ptr->pending_child_task_id != 0 || task_ptr->cancel_scope != DOMINO_TASK_CANCEL_ALL || task_ptr->cancel_target_task_id != 0) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Invalid death stop intent: task=%" PRIu32 " child=%" PRIu32 " scope=%u target=%" PRIu32, task_ptr->task_id,
                                  task_ptr->pending_child_task_id, task_ptr->cancel_scope, task_ptr->cancel_target_task_id);
                return ERR_INVALID_DATA;
            }
            return CODE_OK;
        default:
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Unknown task stop reason: task=%" PRIu32 " reason=%u",
                              task_ptr->task_id, task_ptr->pending_stop_reason);
            return ERR_INVALID_DATA;
    }
}

DOMINO_CODE dominoHumanValidateTaskRecord(const DominoTask* task_ptr) {
    if (!task_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Cannot validate a null task record");
        return ERR_NULL_POINTER;
    }
    if (!task_ptr->behaviors_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Task record has no behavior block: task=%" PRIu32 " count=%u",
                          task_ptr->task_id, task_ptr->behavior_count);
        return ERR_INVALID_DATA;
    }
    if (!dominoHumanGetTaskTypeInfo(task_ptr->type) || task_ptr->channel_index >= DOMINO_HUMAN_TASK_CHANNEL_COUNT ||
        task_ptr->priority_class >= DOMINO_TASK_PRIORITY_COUNT || task_ptr->cancel_scope >= DOMINO_TASK_CANCEL_COUNT) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Invalid task classification: task=%" PRIu32 " type=%d channel=%u priority=%u cancel=%u", task_ptr->task_id, task_ptr->type,
                          task_ptr->channel_index, task_ptr->priority_class, task_ptr->cancel_scope);
        return ERR_INVALID_DATA;
    }
    if (task_ptr->planned_begin_ms > task_ptr->planned_end_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Task planned interval is reversed: task=%" PRIu32 " begin_ms=%" PRIu32 " end_ms=%" PRIu32, task_ptr->task_id,
                          task_ptr->planned_begin_ms, task_ptr->planned_end_ms);
        return ERR_INVALID_DATA;
    }
    if (task_ptr->behavior_count == 0 || task_ptr->current_behavior_index > task_ptr->behavior_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Invalid task behavior cursor: task=%" PRIu32 " count=%u current=%u", task_ptr->task_id, task_ptr->behavior_count,
                          task_ptr->current_behavior_index);
        return ERR_INVALID_DATA;
    }
    DOMINO_CODE err = validateStopIntent(task_ptr);
    if (err != CODE_OK) {
        return err;
    }

    /* 开始／结束时刻只在已启动／终态下有效，不要求未生效字段清零。 */
    const bool terminal_flag = task_ptr->status >= DOMINO_TASK_STATUS_SUCCEEDED;
    if (task_ptr->started_flag && terminal_flag && task_ptr->ended_at_ms < task_ptr->started_at_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Task ends before it starts: task=%" PRIu32 " start_ms=%" PRIu32 " end_ms=%" PRIu32, task_ptr->task_id,
                          task_ptr->started_at_ms, task_ptr->ended_at_ms);
        return ERR_INVALID_DATA;
    }
    if (!terminal_flag && (task_ptr->result != DOMINO_TASK_RESULT_NONE || task_ptr->current_behavior_index == task_ptr->behavior_count)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "Unfinished task has terminal result or exhausted behaviors: task=%" PRIu32 " result=%u current=%u count=%u",
                          task_ptr->task_id, task_ptr->result, task_ptr->current_behavior_index, task_ptr->behavior_count);
        return ERR_INVALID_DATA;
    }
    for (uint8_t index = 0; index < task_ptr->behavior_count; index++) {
        const DominoBehavior* behavior_ptr = &task_ptr->behaviors_ptr[index];
        err = dominoHumanValidateBehaviorRecord(behavior_ptr);
        if (err != CODE_OK) {
            return err;
        }
        const DominoBehaviorProgress* progress_ptr = &behavior_ptr->progress;
        const bool behavior_terminal_flag = progress_ptr->status >= DOMINO_BEHAVIOR_STATUS_DONE;
        const bool completed_flag = progress_ptr->status == DOMINO_BEHAVIOR_STATUS_DONE || progress_ptr->status == DOMINO_BEHAVIOR_STATUS_SKIPPED;
        if (!task_ptr->started_flag && progress_ptr->started_flag) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "Unstarted task has a started behavior: task=%" PRIu32 " behavior=%u", task_ptr->task_id, index);
            return ERR_INVALID_DATA;
        }
        if (task_ptr->started_flag && ((progress_ptr->started_flag && progress_ptr->started_at_ms < task_ptr->started_at_ms) ||
                                       (behavior_terminal_flag && progress_ptr->ended_at_ms < task_ptr->started_at_ms))) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "Behavior predates task: task=%" PRIu32 " behavior=%u task_start_ms=%" PRIu32 " behavior_start_ms=%" PRIu32
                              " behavior_end_ms=%" PRIu32,
                              task_ptr->task_id, index, task_ptr->started_at_ms, progress_ptr->started_at_ms, progress_ptr->ended_at_ms);
            return ERR_INVALID_DATA;
        }
        if (terminal_flag && (!behavior_terminal_flag || task_ptr->ended_at_ms < progress_ptr->ended_at_ms)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "Task ended before its behavior: task=%" PRIu32 " behavior=%u status=%u task_end_ms=%" PRIu32
                              " behavior_end_ms=%" PRIu32,
                              task_ptr->task_id, index, progress_ptr->status, task_ptr->ended_at_ms, progress_ptr->ended_at_ms);
            return ERR_INVALID_DATA;
        }
        if ((task_ptr->status == DOMINO_TASK_STATUS_SUCCEEDED && !completed_flag) ||
            (!terminal_flag && ((index < task_ptr->current_behavior_index && !completed_flag) ||
                                (index > task_ptr->current_behavior_index && progress_ptr->status != DOMINO_BEHAVIOR_STATUS_PENDING)))) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "Behavior status disagrees with task cursor: task=%" PRIu32 " task_status=%u current=%u behavior=%u status=%u",
                              task_ptr->task_id, task_ptr->status, task_ptr->current_behavior_index, index, progress_ptr->status);
            return ERR_INVALID_DATA;
        }
    }

    switch (task_ptr->status) {
        case DOMINO_TASK_STATUS_PLANNED:
            if (task_ptr->started_flag || task_ptr->current_behavior_index != 0 ||
                task_ptr->behaviors_ptr[0].progress.status != DOMINO_BEHAVIOR_STATUS_PENDING) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Planned task already progressed: task=%" PRIu32 " started=%d current=%u behavior_status=%u", task_ptr->task_id,
                                  task_ptr->started_flag, task_ptr->current_behavior_index, task_ptr->behaviors_ptr[0].progress.status);
                return ERR_INVALID_DATA;
            }
            break;
        case DOMINO_TASK_STATUS_RUNNING: {
            const domino_status_t status = task_ptr->behaviors_ptr[task_ptr->current_behavior_index].progress.status;
            if (!task_ptr->started_flag || (status == DOMINO_BEHAVIOR_STATUS_PAUSED && task_ptr->pending_stop_reason == DOMINO_TASK_STOP_NONE) ||
                (status >= DOMINO_BEHAVIOR_STATUS_DONE && (task_ptr->pending_stop_reason == DOMINO_TASK_STOP_NONE ||
                                                           (status != DOMINO_BEHAVIOR_STATUS_DONE && status != DOMINO_BEHAVIOR_STATUS_SKIPPED)))) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Running task has invalid behavior state: task=%" PRIu32 " started=%d behavior_status=%u stop_reason=%u",
                                  task_ptr->task_id, task_ptr->started_flag, status, task_ptr->pending_stop_reason);
                return ERR_INVALID_DATA;
            }
            break;
        }
        case DOMINO_TASK_STATUS_SUSPENDED: {
            const domino_status_t status = task_ptr->behaviors_ptr[task_ptr->current_behavior_index].progress.status;
            if (!task_ptr->started_flag ||
                (status != DOMINO_BEHAVIOR_STATUS_PAUSED && status != DOMINO_BEHAVIOR_STATUS_DONE && status != DOMINO_BEHAVIOR_STATUS_SKIPPED)) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Suspended task has invalid behavior state: task=%" PRIu32 " started=%d behavior_status=%u", task_ptr->task_id,
                                  task_ptr->started_flag, status);
                return ERR_INVALID_DATA;
            }
            break;
        }
        case DOMINO_TASK_STATUS_SUCCEEDED:
            if (!task_ptr->started_flag || task_ptr->result != DOMINO_TASK_RESULT_COMPLETED ||
                task_ptr->current_behavior_index != task_ptr->behavior_count) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Succeeded task is incomplete: task=%" PRIu32 " started=%d result=%u current=%u count=%u", task_ptr->task_id,
                                  task_ptr->started_flag, task_ptr->result, task_ptr->current_behavior_index, task_ptr->behavior_count);
                return ERR_INVALID_DATA;
            }
            break;
        case DOMINO_TASK_STATUS_FAILED:
            if (task_ptr->result != DOMINO_TASK_RESULT_INVALID_TARGET && task_ptr->result != DOMINO_TASK_RESULT_INTERRUPTED) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Failed task has invalid result: task=%" PRIu32 " result=%u", task_ptr->task_id, task_ptr->result);
                return ERR_INVALID_DATA;
            }
            break;
        case DOMINO_TASK_STATUS_CANCELLED:
            if (task_ptr->result != DOMINO_TASK_RESULT_CANCELLED && task_ptr->result != DOMINO_TASK_RESULT_INTERRUPTED) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "Cancelled task has invalid result: task=%" PRIu32 " result=%u", task_ptr->task_id, task_ptr->result);
                return ERR_INVALID_DATA;
            }
            break;
        default:
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "Unknown task status: task=%" PRIu32 " status=%u",
                              task_ptr->task_id, task_ptr->status);
            return ERR_INVALID_DATA;
    }
    return CODE_OK;
}
