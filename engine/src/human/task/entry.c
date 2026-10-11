#include "entry.h"

#include <stddef.h>

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
        return task_ptr->cancel_scope == DOMINO_TASK_CANCEL_NONE && task_ptr->cancel_target_task_id == 0 && task_ptr->pending_child_task_id == 0
                   ? CODE_OK
                   : ERR_INVALID_DATA;
    }
    if (task_ptr->status != DOMINO_TASK_STATUS_RUNNING) {
        return ERR_INVALID_DATA;
    }
    switch (task_ptr->pending_stop_reason) {
        case DOMINO_TASK_STOP_PREEMPT:
            return task_ptr->pending_child_task_id != 0 && task_ptr->cancel_scope == DOMINO_TASK_CANCEL_NONE && task_ptr->cancel_target_task_id == 0
                       ? CODE_OK
                       : ERR_INVALID_DATA;
        case DOMINO_TASK_STOP_CANCEL:
            if (task_ptr->pending_child_task_id != 0 || task_ptr->cancel_scope == DOMINO_TASK_CANCEL_NONE) {
                return ERR_INVALID_DATA;
            }
            return (task_ptr->cancel_scope == DOMINO_TASK_CANCEL_ALL) == (task_ptr->cancel_target_task_id == 0) ? CODE_OK : ERR_INVALID_DATA;
        case DOMINO_TASK_STOP_DANGER:
            return task_ptr->cancel_scope == DOMINO_TASK_CANCEL_NONE && task_ptr->cancel_target_task_id == 0 ? CODE_OK : ERR_INVALID_DATA;
        case DOMINO_TASK_STOP_DEATH:
            return task_ptr->pending_child_task_id == 0 && task_ptr->cancel_scope == DOMINO_TASK_CANCEL_ALL && task_ptr->cancel_target_task_id == 0
                       ? CODE_OK
                       : ERR_INVALID_DATA;
        default:
            return ERR_INVALID_DATA;
    }
}

DOMINO_CODE dominoHumanValidateTaskRecord(const DominoTask* task_ptr) {
    if (!task_ptr || !task_ptr->behaviors_ptr) {
        return ERR_NULL_POINTER;
    }
    if (!dominoHumanGetTaskTypeInfo(task_ptr->type) || task_ptr->status >= DOMINO_TASK_STATUS_COUNT ||
        task_ptr->channel_index >= DOMINO_HUMAN_TASK_CHANNEL_COUNT || task_ptr->priority_class >= DOMINO_TASK_PRIORITY_COUNT ||
        task_ptr->cancel_scope >= DOMINO_TASK_CANCEL_COUNT || task_ptr->planned_begin_ms > task_ptr->planned_end_ms ||
        task_ptr->behavior_count == 0 || task_ptr->current_behavior_index > task_ptr->behavior_count) {
        return ERR_INVALID_DATA;
    }
    DOMINO_CODE err = validateStopIntent(task_ptr);
    if (err != CODE_OK) {
        return err;
    }

    /* 开始／结束时刻只在已启动／终态下有效，不要求未生效字段清零。 */
    const bool terminal_flag = task_ptr->status >= DOMINO_TASK_STATUS_SUCCEEDED;
    if (task_ptr->started_flag && terminal_flag && task_ptr->ended_at_ms < task_ptr->started_at_ms) {
        return ERR_INVALID_DATA;
    }
    if (!terminal_flag && (task_ptr->result != DOMINO_TASK_RESULT_NONE || task_ptr->current_behavior_index == task_ptr->behavior_count)) {
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
        if ((!task_ptr->started_flag && progress_ptr->started_flag) ||
            (task_ptr->started_flag && ((progress_ptr->started_flag && progress_ptr->started_at_ms < task_ptr->started_at_ms) ||
                                        (behavior_terminal_flag && progress_ptr->ended_at_ms < task_ptr->started_at_ms))) ||
            (terminal_flag && (!behavior_terminal_flag || task_ptr->ended_at_ms < progress_ptr->ended_at_ms))) {
            return ERR_INVALID_DATA;
        }
        if ((task_ptr->status == DOMINO_TASK_STATUS_SUCCEEDED && !completed_flag) ||
            (!terminal_flag && ((index < task_ptr->current_behavior_index && !completed_flag) ||
                                (index > task_ptr->current_behavior_index && progress_ptr->status != DOMINO_BEHAVIOR_STATUS_PENDING)))) {
            return ERR_INVALID_DATA;
        }
    }

    switch (task_ptr->status) {
        case DOMINO_TASK_STATUS_PLANNED:
            if (task_ptr->started_flag || task_ptr->current_behavior_index != 0 ||
                task_ptr->behaviors_ptr[0].progress.status != DOMINO_BEHAVIOR_STATUS_PENDING) {
                return ERR_INVALID_DATA;
            }
            break;
        case DOMINO_TASK_STATUS_RUNNING: {
            const domino_status_t status = task_ptr->behaviors_ptr[task_ptr->current_behavior_index].progress.status;
            if (!task_ptr->started_flag || (status == DOMINO_BEHAVIOR_STATUS_PAUSED && task_ptr->pending_stop_reason == DOMINO_TASK_STOP_NONE) ||
                (status >= DOMINO_BEHAVIOR_STATUS_DONE && (task_ptr->pending_stop_reason == DOMINO_TASK_STOP_NONE ||
                                                           (status != DOMINO_BEHAVIOR_STATUS_DONE && status != DOMINO_BEHAVIOR_STATUS_SKIPPED)))) {
                return ERR_INVALID_DATA;
            }
            break;
        }
        case DOMINO_TASK_STATUS_SUSPENDED: {
            const domino_status_t status = task_ptr->behaviors_ptr[task_ptr->current_behavior_index].progress.status;
            if (!task_ptr->started_flag ||
                (status != DOMINO_BEHAVIOR_STATUS_PAUSED && status != DOMINO_BEHAVIOR_STATUS_DONE && status != DOMINO_BEHAVIOR_STATUS_SKIPPED)) {
                return ERR_INVALID_DATA;
            }
            break;
        }
        case DOMINO_TASK_STATUS_SUCCEEDED:
            if (!task_ptr->started_flag || task_ptr->result != DOMINO_TASK_RESULT_COMPLETED ||
                task_ptr->current_behavior_index != task_ptr->behavior_count) {
                return ERR_INVALID_DATA;
            }
            break;
        case DOMINO_TASK_STATUS_FAILED:
            if (task_ptr->result != DOMINO_TASK_RESULT_INVALID_TARGET && task_ptr->result != DOMINO_TASK_RESULT_INTERRUPTED) {
                return ERR_INVALID_DATA;
            }
            break;
        case DOMINO_TASK_STATUS_CANCELLED:
            if (task_ptr->result != DOMINO_TASK_RESULT_CANCELLED && task_ptr->result != DOMINO_TASK_RESULT_INTERRUPTED) {
                return ERR_INVALID_DATA;
            }
            break;
        default:
            return ERR_INVALID_DATA;
    }
    return CODE_OK;
}
