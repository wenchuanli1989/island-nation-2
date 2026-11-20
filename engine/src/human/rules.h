#ifndef DOMINO_ENGINE_HUMAN_RULES_H
#define DOMINO_ENGINE_HUMAN_RULES_H

#include <stdint.h>

#include "../queue/message.h"
#include "domino_shared_types.h"

/** @file rules.h
 * @brief NPC AI 第一阶段的数值规则、容量上限及三类规划器／执行器配置。
 * @note 全部时长使用未冻结现实毫秒，不应用游戏日历倍率。配置在 human Init 时固定，本会话内不变。
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

/** @brief 候选总内存预算：规划临时数据、已打包载荷、结果队列中及执行器处理中的载荷合计。 */
#define DOMINO_HUMAN_CANDIDATE_BUDGET_BYTES ((uint64_t)256U * 1024U * 1024U)

/** @brief 单类规划器配置；规划请求不设期限，每个有效请求在本会话内必然得到一个结果。 */
typedef struct DominoHumanPlannerConfig {
    uint32_t high_quota;      ///< 每批最多处理的高优先级请求数 K；普通请求配额固定为 1。
    uint32_t compute_budget;  ///< 单请求计算预算（工作单位），耗尽返回暂时无方案。
} DominoHumanPlannerConfig;

/** @brief 本会话固定的 NPC AI 配置。 */
typedef struct DominoHumanAiConfig {
    DominoHumanPlannerConfig planner_list[DOMINO_HUMAN_PLANNER_COUNT];
    /// 正延迟，三类用途：行为规划在无方案、拒绝或处理后仍需规划时的重试；动机和任务整份候选被拒时的兜底下次规划时刻；
    /// 第一阶段动机和任务策略在计算预算耗尽（NO_PLAN）时给出的下次规划时刻。
    domino_runtime_ms_t retry_delay_ms;
    uint32_t result_quota;         ///< 执行器每批从每条结果队列最多处理的消息数。
    uint32_t inbox_quota;          ///< 执行器每批最多处理的业务收件箱消息数。
    uint32_t todo_quota;           ///< 执行器每批另外处理的本地待办数。
    uint8_t sleep_trigger_energy;  ///< 任务规划策略：预测精力不高于此值且上一轮已结束时提出下一轮睡眠任务。
    uint8_t sleep_target_energy;   ///< 睡眠任务的完成精力。
    uint8_t urgent_energy;         ///< 精力不高于此值时规划请求使用高优先级。
} DominoHumanAiConfig;

enum {
    DOMINO_HUMAN_EXECUTOR_BATCH_MESSAGE_MAX = 64U,  ///< 六条结果队列与业务收件箱的每批消息总额。
    DOMINO_HUMAN_PLANNER_HIGH_QUOTA_MAX = 64U,      ///< K 的资源上限，限定每批临时存储。
};

/** @brief 当前会话配置；human Init 写入，之后只读。 */
extern DominoHumanAiConfig g_domino_human_ai_config;

/** @brief 返回内置默认配置：K=4、计算预算 512、重试延迟 3000ms，结果队列各 8、收件箱 16、待办 16，
 * 睡眠触发 30、完成 80、紧急 10。
 */
DominoHumanAiConfig dominoHumanDefaultAiConfig(void);

/** @brief human Init 校验并固定本会话配置；不合法配置致命退出。
 * @note playground 可在引擎 Init 前通过 dominoHumanSetNextAiConfig() 指定下一轮配置。
 */
void dominoHumanAiConfigInit(void);

/** @brief human Destroy 复位本会话配置。 */
void dominoHumanAiConfigDestroy(void);

#ifdef DOMINO_PLAYGROUND_MODE
/** @brief 仅 playground：指定此后 human Init 使用的配置；传空指针恢复默认。进程级设置，不随实例 Destroy 清空。 */
void dominoHumanSetNextAiConfig(const DominoHumanAiConfig* config_ptr);
#endif

#endif
