#include "entry.h"

#include <string.h>

#include "../rules.h"
bool dominoMotivationNeedsReassessment(const DominoMotivation* motivation, const DominoHumanData* data) {
    return motivation->status == DOMINO_MOTIVATION_VALID &&
           (motivation->reassess || motivation->needs_action != (data->energy <= DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY));
}
bool dominoMotivationNeedsTask(const DominoMotivation* motivation, const DominoHumanData* data) {
    if (motivation->status != DOMINO_MOTIVATION_VALID || !motivation->needs_action || motivation->failed_work || !data->sleep_allowed ||
        data->energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY)
        return false;
    for (uint8_t i = 0; i < data->task_count; ++i) {
        const DominoTask* task = dominoGetTaskByID(data->task_id_list[i]);
        if (task->motivation_id == motivation->motivation_id && (!task->has_execution || task->execution.status == DOMINO_TASK_STATUS_OPEN))
            return false;
    }
    return motivation->work_round == motivation->closed_round;
}
void dominoMotivationCloseWork(DominoMotivation* motivation, const DominoTask* task, bool failed, uint32_t reason) {
    if (task->work_round > motivation->closed_round)
        motivation->closed_round = task->work_round;
    if (failed) {
        motivation->failed_work = true;
        motivation->last_failure_reason = reason;
    }
    motivation->reassess = true;
    motivation->needs_action = false;
}
void dominoMotivationRecordSuccess(DominoMotivation* motivation, const DominoTask* task) {
    if (motivation->implementation_count == UINT64_MAX)
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "motivation count overflow");
    ++motivation->implementation_count;
    if (motivation->history_count == DOMINO_MOTIVATION_HISTORY_MAX) {
        memmove(motivation->history, motivation->history + 1, (DOMINO_MOTIVATION_HISTORY_MAX - 1) * sizeof(motivation->history[0]));
        --motivation->history_count;
    }
    motivation->history[motivation->history_count++] =
        (DominoMotivationImplementation){task->task_id, task->execution.started_at_ms, task->execution.ended_at_ms};
}
