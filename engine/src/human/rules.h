#ifndef DOMINO_ENGINE_HUMAN_RULES_H
#define DOMINO_ENGINE_HUMAN_RULES_H

#include <stdint.h>

#include "../queue/message.h"
#include "domino_shared_types.h"

/** @file rules.h
 * @brief NPC AI 第一阶段的数值规则、容量上限和规划器／执行器配置。
 * @note 时长均为未冻结现实毫秒；配置在 human Init 时固定，本会话内不变。
 */

/** @brief 自然需求、睡眠及容量的固定规则。 */
enum {
    DOMINO_HUMAN_NEED_MINUTE_MS = 60000U,  ///< 自然需求离散变化周期。
    DOMINO_HUMAN_ENERGY_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_FOOD_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_WATER_DECAY_PER_MINUTE = 2U,
    DOMINO_HUMAN_HYGIENE_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_RECREATION_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_SOCIAL_DECAY_PER_MINUTE = 1U,
    DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS = 6000U,  ///< 每累计 6000ms 有效睡眠恢复 1 点精力。

    DOMINO_HUMAN_WORLD_COUNT_MAX = 10000U,
    DOMINO_HUMAN_MOTIVATION_POOL_MAX = 80000U,
    DOMINO_HUMAN_TASK_POOL_MAX = 320000U,
    DOMINO_HUMAN_PLAN_ITEM_POOL_MAX = 1000000U,
    DOMINO_HUMAN_BEHAVIOR_POOL_MAX = 1000000U,

    DOMINO_HUMAN_PREDICT_STEP_MAX = 1024U,  ///< 单次只读预测最多推进的效果边界数，超出后返回保守检查时刻。
};

/** @brief 全部在途候选块（规划中、结果队列中、执行器处理中）的内存总预算。 */
#define DOMINO_HUMAN_CANDIDATE_BUDGET_BYTES ((uint64_t)256U * 1024U * 1024U)

/** @brief 单类规划器配置。 */
typedef struct DominoHumanPlannerConfig {
    uint32_t high_quota;      ///< 每批最多处理的高优先级请求数 K；普通请求配额固定为 1。
    uint32_t compute_budget;  ///< 单请求计算预算（工作单位），耗尽返回暂时无方案。
} DominoHumanPlannerConfig;

/** @brief 本会话固定的 NPC AI 配置。 */
typedef struct DominoHumanAiConfig {
    DominoHumanPlannerConfig planner_list[DOMINO_HUMAN_PLANNER_COUNT];
    /// 正延迟：行为规划无方案、被拒或仍需规划时的重试间隔；动机和任务候选整份被拒时的兜底下次规划间隔；
    /// 第一阶段策略返回 NO_PLAN 时给出的下次规划间隔。
    domino_runtime_ms_t retry_delay_ms;
    uint32_t result_quota;         ///< 执行器每批从每条结果队列最多处理的消息数。
    uint32_t inbox_quota;          ///< 执行器每批最多处理的业务收件箱消息数。
    uint32_t todo_quota;           ///< 执行器每批另外处理的本地待办数。
    uint8_t sleep_trigger_energy;  ///< 预测精力降到此值且上一轮已结束时，提出下一轮睡眠任务。
    uint8_t sleep_target_energy;   ///< 睡眠任务的完成精力。
    uint8_t urgent_energy;         ///< 精力不高于此值时规划请求使用高优先级。
} DominoHumanAiConfig;

enum {
    DOMINO_HUMAN_EXECUTOR_BATCH_MESSAGE_MAX = 64U,  ///< 六条结果队列与业务收件箱的每批消息总额。
    DOMINO_HUMAN_PLANNER_HIGH_QUOTA_MAX = 64U,      ///< high_quota 上限，决定每批临时存储。
};

/** @brief 本会话配置；human Init 写入，之后只读。 */
extern DominoHumanAiConfig g_domino_human_ai_config;

/** @brief 校验并固定本会话配置（内置默认配置）；不合法致命退出。 */
void dominoHumanAiConfigInit(void);

/** @brief 清零本会话配置。 */
void dominoHumanAiConfigDestroy(void);

#endif
