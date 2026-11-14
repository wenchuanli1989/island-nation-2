#include "data.h"

#include <string.h>

#include "../time/entry.h"
#include "domino_shared_common.h"
#include "executor.h"
#include "rules.h"
static void reassess(DominoHuman* human) {
    human->data->motivation_retry.valid = false;
    for (uint8_t i = 0; i < human->data->motivation_count; ++i) {
        DominoMotivation* motivation = dominoGetMotivationByID(human->data->motivation_ids[i]);
        motivation->reassess = true;
        // 已恢复到充足精力，后续下降才构成新的独立睡眠需求。
        if (human->data->energy >= DOMINO_HUMAN_SLEEP_SATISFIED_ENERGY)
            motivation->failed_work = false;
        motivation->retry.valid = false;
    }
    for (uint8_t i = 0; i < human->data->task_count; ++i)
        dominoGetTaskByID(human->data->task_id_list[i])->retry.valid = false;
}
void dominoHumanApplySleepCondition(DominoHuman* human, bool allowed) {
    human->data->sleep_allowed = allowed;
    reassess(human);
}
void dominoHumanApplyEnergy(DominoHuman* human, uint8_t energy) {
    human->data->energy = energy;
    reassess(human);
}
static DOMINO_CODE permitted(const DominoHuman* human, uint64_t client_id) {
    if (!human)
        return ERR_NOT_FOUND;
    return client_id && human->data->manager_client_id == client_id ? CODE_OK : ERR_GAME_STATE_INVALID;
}
static void copySummary(const DominoHuman* human, DominoNpcSummary* summary) {
    const DominoHumanData* data = human->data;
    *summary = (DominoNpcSummary){.name_version = data->display_name_version,
                                  .human_id = human->human_id,
                                  .settled_at_ms = data->settled_at_ms,
                                  .energy = data->energy,
                                  .food = data->food,
                                  .water = data->water,
                                  .hygiene = data->hygiene,
                                  .recreation = data->recreation,
                                  .social = data->social,
                                  .motivation_count = data->motivation_count,
                                  .status = human->status};
    dominoTimeGetRuntimeMs(&summary->read_at_ms);
    memcpy(summary->name, data->display_name, sizeof(summary->name));
    for (uint8_t i = 0; i < data->motivation_count; ++i) {
        uint64_t count = dominoGetMotivationByID(data->motivation_ids[i])->implementation_count;
        if (count > UINT64_MAX - summary->implementation_count)
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "summary count overflow");
        summary->implementation_count += count;
    }
    for (uint8_t c = 0; c < DOMINO_HUMAN_TASK_CHANNEL_COUNT; ++c) {
        const DominoTask* task = dominoGetTaskByID(data->active_task_id_list[c]);
        if (!task)
            continue;
        summary->channels[c].task_id = task->task_id;
        summary->channels[c].task_type = task->type;
        const DominoBehavior* behavior = dominoGetBehaviorByID(task->execution.current_behavior_id);
        if (behavior) {
            summary->channels[c].behavior_id = behavior->behavior_id;
            summary->channels[c].behavior_type = behavior->plan.type;
            summary->channels[c].behavior_status = (uint8_t)behavior->status;
            summary->channels[c].active_elapsed_ms = behavior->active_elapsed_ms;
        }
    }
}
void dominoHumanQueryNpc(uint64_t client_id, const DominoNpcRequest* request, DominoNpcResult* result) {
    *result = (DominoNpcResult){.session_id = request->session_id, .request_id = request->request_id, .status = DOMINO_NPC_REJECTED};
    dominoHumanReadLock();
    const DominoHuman* human = dominoGetHumanByID(request->human_id);
    result->reason = permitted(human, client_id);
    if (result->reason == CODE_OK) {
        copySummary(human, &result->summary);
        result->status = DOMINO_NPC_QUERY_COMPLETE;
    }
    dominoHumanReadUnlock();
}
void dominoHumanSetDisplayName(uint64_t client_id, const DominoNpcRequest* request, DominoNpcResult* result) {
    *result = (DominoNpcResult){.session_id = request->session_id, .request_id = request->request_id, .status = DOMINO_NPC_REJECTED};
    DominoHuman* human = dominoGetHumanByID(request->human_id);
    result->reason = permitted(human, client_id);
    if (result->reason != CODE_OK)
        return;
    DominoHumanData* data = human->data;
    if (request->expected_name_version != data->display_name_version)
        result->status = DOMINO_NPC_VERSION_CONFLICT;
    else if (strcmp(request->name, data->display_name) == 0)
        result->status = DOMINO_NPC_UNCHANGED;
    else {
        if (data->display_name_version == UINT64_MAX)
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "display name version exhausted");
        memcpy(data->display_name, request->name, request->name_len + 1U);
        ++data->display_name_version;
        result->status = DOMINO_NPC_NAME_CHANGED;
    }
    copySummary(human, &result->summary);
}
void dominoHumanRequestNameChange(uint64_t client_id, const DominoNpcRequest* request) {
    DominoHumanExecutionMessage message = {.type = DOMINO_HUMAN_EXECUTION_MESSAGE_NAME, .payload.name = {client_id, *request}};
    dominoHumanExecutionPost(&message);
}
DOMINO_CODE dominoHumanUpdateSleepCondition(uint32_t human_id, bool allowed) {
    if (!human_id)
        return ERR_INVALID_PARAM;
    DominoHumanExecutionMessage message = {.type = DOMINO_HUMAN_EXECUTION_MESSAGE_SLEEP_CONDITION,
                                           .payload.condition = {dominoMonotonicTimeNs(), human_id, allowed}};
    dominoHumanExecutionPost(&message);
    return CODE_OK;
}
DOMINO_CODE dominoHumanUpdateEnergy(uint32_t human_id, uint8_t energy) {
    if (!human_id || energy > DOMINO_HUMAN_NEED_MAX)
        return ERR_INVALID_PARAM;
    DominoHumanExecutionMessage message = {.type = DOMINO_HUMAN_EXECUTION_MESSAGE_ENERGY,
                                           .payload.energy = {dominoMonotonicTimeNs(), human_id, energy}};
    dominoHumanExecutionPost(&message);
    return CODE_OK;
}
