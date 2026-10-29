#include "entry.h"

#include <stddef.h>

static const DominoBehaviorTypeInfo g_behavior_type_info[DOMINO_BEHAVIOR_TYPE_COUNT] = {
    [DOMINO_BEHAVIOR_TYPE_MOVE] = {.name_str = "move"},           [DOMINO_BEHAVIOR_TYPE_EAT] = {.name_str = "eat"},
    [DOMINO_BEHAVIOR_TYPE_WASH] = {.name_str = "wash"},           [DOMINO_BEHAVIOR_TYPE_WORK] = {.name_str = "work"},
    [DOMINO_BEHAVIOR_TYPE_STUDY] = {.name_str = "study"},         [DOMINO_BEHAVIOR_TYPE_SHOP] = {.name_str = "shop"},
    [DOMINO_BEHAVIOR_TYPE_EXERCISE] = {.name_str = "exercise"},   [DOMINO_BEHAVIOR_TYPE_TREAT] = {.name_str = "treat"},
    [DOMINO_BEHAVIOR_TYPE_CHAT] = {.name_str = "chat"},           [DOMINO_BEHAVIOR_TYPE_SIGHTSEE] = {.name_str = "sightsee"},
    [DOMINO_BEHAVIOR_TYPE_PARADE] = {.name_str = "parade"},       [DOMINO_BEHAVIOR_TYPE_SLEEP] = {.name_str = "sleep"},
    [DOMINO_BEHAVIOR_TYPE_ENTERTAIN] = {.name_str = "entertain"}, [DOMINO_BEHAVIOR_TYPE_PHONE_CALL] = {.name_str = "phone_call"},
    [DOMINO_BEHAVIOR_TYPE_MESSAGE] = {.name_str = "message"},
};

const DominoBehaviorTypeInfo* dominoHumanGetBehaviorTypeInfo(DOMINO_BEHAVIOR_TYPE type) {
    if (type <= DOMINO_BEHAVIOR_TYPE_NONE || type >= DOMINO_BEHAVIOR_TYPE_COUNT) {
        return NULL;
    }
    return &g_behavior_type_info[type];
}

DOMINO_CODE dominoHumanValidateBehaviorRecord(const DominoBehavior* behavior_ptr) {
    if (!behavior_ptr) {
        return ERR_NULL_POINTER;
    }
    if (!dominoHumanGetBehaviorTypeInfo(behavior_ptr->type) || behavior_ptr->planned_begin_ms > behavior_ptr->planned_end_ms) {
        return ERR_INVALID_DATA;
    }

    const DominoBehaviorProgress* progress_ptr = &behavior_ptr->progress;
    if (progress_ptr->wait_reason >= DOMINO_BEHAVIOR_WAIT_COUNT) {
        return ERR_INVALID_DATA;
    }

    /* 时间戳只在对应的已启动、终态或等待状态下有效，不要求未生效字段清零。 */
    const bool terminal_flag = progress_ptr->status >= DOMINO_BEHAVIOR_STATUS_DONE;
    if (progress_ptr->started_flag) {
        if (progress_ptr->accounted_at_ms < progress_ptr->started_at_ms ||
            progress_ptr->active_elapsed_ms > progress_ptr->accounted_at_ms - progress_ptr->started_at_ms ||
            (terminal_flag && progress_ptr->ended_at_ms < progress_ptr->accounted_at_ms)) {
            return ERR_INVALID_DATA;
        }
    } else if (progress_ptr->active_elapsed_ms != 0) {
        return ERR_INVALID_DATA;
    }

    if (progress_ptr->status == DOMINO_BEHAVIOR_STATUS_WAITING) {
        // wait_reason 表示截止点有效；0 也是合法绝对时刻，恢复时允许截止点已经到期。
        if (progress_ptr->wait_reason == DOMINO_BEHAVIOR_WAIT_NONE || progress_ptr->wait_until_ms < progress_ptr->started_at_ms) {
            return ERR_INVALID_DATA;
        }
    } else if (progress_ptr->wait_reason != DOMINO_BEHAVIOR_WAIT_NONE) {
        return ERR_INVALID_DATA;
    }

    switch (progress_ptr->status) {
        case DOMINO_BEHAVIOR_STATUS_PENDING:
        case DOMINO_BEHAVIOR_STATUS_SKIPPED:
            if (progress_ptr->started_flag) {
                return ERR_INVALID_DATA;
            }
            break;
        case DOMINO_BEHAVIOR_STATUS_ACTIVE:
        case DOMINO_BEHAVIOR_STATUS_WAITING:
        case DOMINO_BEHAVIOR_STATUS_PAUSED:
        case DOMINO_BEHAVIOR_STATUS_DONE:
            if (!progress_ptr->started_flag) {
                return ERR_INVALID_DATA;
            }
            break;
        case DOMINO_BEHAVIOR_STATUS_FAILED:
        case DOMINO_BEHAVIOR_STATUS_CANCELLED:
            break;
        default:
            return ERR_INVALID_DATA;
    }

    if (behavior_ptr->type == DOMINO_BEHAVIOR_TYPE_SLEEP) {
        const domino_runtime_ms_t target_active_ms = behavior_ptr->parameters.sleep.target_active_ms;
        if (target_active_ms == 0 || progress_ptr->active_elapsed_ms > target_active_ms ||
            (progress_ptr->status >= DOMINO_BEHAVIOR_STATUS_ACTIVE && progress_ptr->status <= DOMINO_BEHAVIOR_STATUS_PAUSED &&
             progress_ptr->active_elapsed_ms == target_active_ms) ||
            (progress_ptr->status == DOMINO_BEHAVIOR_STATUS_DONE && progress_ptr->active_elapsed_ms != target_active_ms)) {
            return ERR_INVALID_DATA;
        }
    }
    return CODE_OK;
}
