#include "entry.h"

#include "../entry.h"
#include "domino_shared_common.h"

DominoMotivation* dominoHumanFindActiveMotivation(const DominoHumanData* data_ptr, uint8_t type) {
    for (uint8_t index = 0U; index < data_ptr->motivation_count; index++) {
        DominoMotivation* motivation_ptr = dominoGetMotivationById(data_ptr->motivation_id_list[index]);
        if (motivation_ptr->type == type && motivation_ptr->status == DOMINO_MOTIVATION_STATUS_ACTIVE) {
            return motivation_ptr;
        }
    }
    return nullptr;
}

bool dominoHumanHasActiveMotivation(const DominoHumanData* data_ptr) {
    for (uint8_t index = 0U; index < data_ptr->motivation_count; index++) {
        if (dominoGetMotivationById(data_ptr->motivation_id_list[index])->status == DOMINO_MOTIVATION_STATUS_ACTIVE) {
            return true;
        }
    }
    return false;
}

void dominoHumanMotivationRecordTaskEnded(DominoMotivation* motivation_ptr, const DominoTask* task_ptr) {
    if (motivation_ptr->round_task_id == task_ptr->task_id) {
        motivation_ptr->round_task_id = 0U;
    }
    if (task_ptr->exec_status != DOMINO_TASK_EXEC_STATUS_SUCCEEDED) {
        motivation_ptr->last_failure_reason = task_ptr->end_reason;
        return;
    }
    if (motivation_ptr->success_count == UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "motivation=%u success count overflows", motivation_ptr->motivation_id);
    }
    motivation_ptr->success_count++;
    const DominoMotivationHistory entry = {
        .task_id = task_ptr->task_id, .started_at_ms = task_ptr->started_at_ms, .ended_at_ms = task_ptr->ended_at_ms};
    if (motivation_ptr->history_count < DOMINO_MOTIVATION_HISTORY_COUNT_MAX) {
        const uint8_t slot = (uint8_t)((motivation_ptr->history_head_index + motivation_ptr->history_count) % DOMINO_MOTIVATION_HISTORY_COUNT_MAX);
        motivation_ptr->history_list[slot] = entry;
        motivation_ptr->history_count++;
    } else {
        // 已满：覆盖最早一条，最早位置后移。
        motivation_ptr->history_list[motivation_ptr->history_head_index] = entry;
        motivation_ptr->history_head_index = (uint8_t)((motivation_ptr->history_head_index + 1U) % DOMINO_MOTIVATION_HISTORY_COUNT_MAX);
    }
}

DominoMotivationHistory dominoHumanMotivationHistoryAt(const DominoMotivation* motivation_ptr, uint8_t index) {
    return motivation_ptr->history_list[(motivation_ptr->history_head_index + index) % DOMINO_MOTIVATION_HISTORY_COUNT_MAX];
}
