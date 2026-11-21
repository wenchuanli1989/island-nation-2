#include "rules.h"

#include <string.h>

#include "domino_shared_common.h"
#include "domino_shared_human.h"

static_assert(DOMINO_HUMAN_NEED_MINUTE_MS > 0U, "natural need interval must be positive");
static_assert(DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS > 0U, "sleep recovery interval must be positive");
static_assert(DOMINO_HUMAN_NEED_MINUTE_MS / DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS > DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE,
              "sleep must restore energy faster than natural decay");

DominoHumanAiConfig g_domino_human_ai_config;

#ifdef DOMINO_PLAYGROUND_MODE
static DominoHumanAiConfig g_next_config;
static bool g_next_config_flag;
#endif

DominoHumanAiConfig dominoHumanDefaultAiConfig(void) {
    DominoHumanAiConfig config = {
        .retry_delay_ms = 3000U,
        .result_quota = 8U,
        .inbox_quota = 16U,
        .todo_quota = 16U,
        .sleep_trigger_energy = 30U,
        .sleep_target_energy = 80U,
        .urgent_energy = 10U,
    };
    for (uint32_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        config.planner_list[kind] = (DominoHumanPlannerConfig){.high_quota = 4U, .compute_budget = 512U};
    }
    return config;
}

void dominoHumanAiConfigInit(void) {
    DominoHumanAiConfig config = dominoHumanDefaultAiConfig();
#ifdef DOMINO_PLAYGROUND_MODE
    if (g_next_config_flag) {
        config = g_next_config;
    }
#endif
    for (uint32_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        const DominoHumanPlannerConfig* planner_ptr = &config.planner_list[kind];
        // high_quota 受每批临时存储上限约束，保证每批工作有限。
        if (planner_ptr->high_quota == 0U || planner_ptr->high_quota > DOMINO_HUMAN_PLANNER_HIGH_QUOTA_MAX || planner_ptr->compute_budget == 0U) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "invalid human planner config kind=%u", kind);
        }
    }
    const uint64_t message_total =
        ((uint64_t)config.result_quota * DOMINO_HUMAN_PLANNER_COUNT * DOMINO_HUMAN_PLAN_PRIORITY_COUNT) + config.inbox_quota;
    if (config.retry_delay_ms == 0U || config.result_quota == 0U || config.inbox_quota == 0U || config.todo_quota == 0U ||
        message_total > DOMINO_HUMAN_EXECUTOR_BATCH_MESSAGE_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "invalid human executor quotas");
    }
    if (config.sleep_trigger_energy >= config.sleep_target_energy || config.sleep_target_energy > DOMINO_HUMAN_NEED_MAX ||
        config.urgent_energy > config.sleep_trigger_energy) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "invalid human sleep thresholds");
    }
    g_domino_human_ai_config = config;
}

void dominoHumanAiConfigDestroy(void) {
    // playground 指定的配置是进程级设置，不在这里清除。
    memset(&g_domino_human_ai_config, 0, sizeof(g_domino_human_ai_config));
}

#ifdef DOMINO_PLAYGROUND_MODE
void dominoHumanSetNextAiConfig(const DominoHumanAiConfig* config_ptr) {
    if (config_ptr == nullptr) {
        g_next_config_flag = false;
        return;
    }
    g_next_config = *config_ptr;
    g_next_config_flag = true;
}
#endif
