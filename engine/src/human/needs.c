#include "needs.h"

#include "../logger/entry.h"
#include "rules.h"

static_assert((DOMINO_HUMAN_NEED_MAX - DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY - 1U) / DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE + 1U <=
                  UINT32_MAX / DOMINO_HUMAN_NEED_MINUTE_MS,
              "Maximum sleep trigger interval must fit runtime milliseconds");

DOMINO_CODE dominoHumanValidateNeedSettlement(const DominoHumanData* data_ptr, domino_runtime_ms_t now_ms) {
    if (!data_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "需求结算的人物 data 参数为空");
        return ERR_NULL_POINTER;
    }
    if (data_ptr->energy > DOMINO_HUMAN_NEED_MAX || data_ptr->food > DOMINO_HUMAN_NEED_MAX || data_ptr->water > DOMINO_HUMAN_NEED_MAX ||
        data_ptr->hygiene > DOMINO_HUMAN_NEED_MAX || data_ptr->recreation > DOMINO_HUMAN_NEED_MAX || data_ptr->social > DOMINO_HUMAN_NEED_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "人物需求值超出有效范围: human_id=%u, energy=%u, food=%u, water=%u, hygiene=%u, recreation=%u, social=%u",
                          data_ptr->human_id, data_ptr->energy, data_ptr->food, data_ptr->water, data_ptr->hygiene, data_ptr->recreation,
                          data_ptr->social);
        return ERR_INVALID_DATA;
    }
    if (now_ms < data_ptr->needs_updated_at_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "需求结算时刻早于已结算检查点: human_id=%u, now_ms=%u, needs_updated_at_ms=%u", data_ptr->human_id, now_ms,
                          data_ptr->needs_updated_at_ms);
        return ERR_INVALID_PARAM;
    }
    return CODE_OK;
}

static DOMINO_CODE validateIdleNeedSettlement(const DominoHumanData* data_ptr, domino_runtime_ms_t now_ms) {
    DOMINO_CODE code = dominoHumanValidateNeedSettlement(data_ptr, now_ms);
    if (code != CODE_OK) {
        return code;
    }
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        if (data_ptr->active_task_id_list[channel] != 0U) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "人物存在活动任务，不能独立结算空闲需求: human_id=%u, channel=%u, task_id=%u", data_ptr->human_id, channel,
                              data_ptr->active_task_id_list[channel]);
            return ERR_GAME_STATE_INVALID;
        }
    }
    return CODE_OK;
}

DOMINO_CODE dominoHumanGetSleepTriggerMs(const DominoHumanData* data_ptr, domino_runtime_ms_t now_ms, domino_runtime_ms_t* due_out_ptr) {
    if (data_ptr->energy <= DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        *due_out_ptr = now_ms;
        return CODE_OK;
    }

    const uint32_t energy_gap = data_ptr->energy - DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY;
    const uint32_t minute_count = (energy_gap - 1U) / DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE + 1U;
    const domino_runtime_ms_t delta_ms = minute_count * DOMINO_HUMAN_NEED_MINUTE_MS;
    if (delta_ms > UINT32_MAX - data_ptr->needs_updated_at_ms) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "睡眠触发时刻超出累计毫秒范围: human_id=%u, needs_updated_at_ms=%u, delta_ms=%u", data_ptr->human_id,
                          data_ptr->needs_updated_at_ms, delta_ms);
        return ERR_OUT_OF_RANGE;
    }
    const domino_runtime_ms_t trigger_ms = data_ptr->needs_updated_at_ms + delta_ms;
    *due_out_ptr = trigger_ms > now_ms ? trigger_ms : now_ms;
    return CODE_OK;
}

void dominoHumanApplyNaturalNeeds(DominoHumanData* data_ptr, domino_runtime_ms_t now_ms) {
    const domino_runtime_ms_t elapsed_ms = now_ms - data_ptr->needs_updated_at_ms;
    const uint32_t minute_count = elapsed_ms / DOMINO_HUMAN_NEED_MINUTE_MS;
    if (minute_count == 0U) {
        return;
    }

    data_ptr->energy = dominoHumanDecayNeed(data_ptr->energy, minute_count, DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE);
    data_ptr->food = dominoHumanDecayNeed(data_ptr->food, minute_count, DOMINO_HUMAN_FOOD_DECAY_PER_MINUTE);
    data_ptr->water = dominoHumanDecayNeed(data_ptr->water, minute_count, DOMINO_HUMAN_WATER_DECAY_PER_MINUTE);
    data_ptr->hygiene = dominoHumanDecayNeed(data_ptr->hygiene, minute_count, DOMINO_HUMAN_HYGIENE_DECAY_PER_MINUTE);
    data_ptr->recreation = dominoHumanDecayNeed(data_ptr->recreation, minute_count, DOMINO_HUMAN_RECREATION_DECAY_PER_MINUTE);
    data_ptr->social = dominoHumanDecayNeed(data_ptr->social, minute_count, DOMINO_HUMAN_SOCIAL_DECAY_PER_MINUTE);
    data_ptr->needs_updated_at_ms = now_ms - elapsed_ms % DOMINO_HUMAN_NEED_MINUTE_MS;
}

DOMINO_CODE dominoHumanSettleNeeds(DominoHumanData* data_ptr, domino_runtime_ms_t now_ms) {
    DOMINO_CODE code = validateIdleNeedSettlement(data_ptr, now_ms);
    if (code != CODE_OK) {
        return code;
    }
    dominoHumanApplyNaturalNeeds(data_ptr, now_ms);
    return CODE_OK;
}
