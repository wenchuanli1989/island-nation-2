#ifndef DOMINO_SHARED_HUMAN_TASK_H
#define DOMINO_SHARED_HUMAN_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_types.h"

/** @file domino_shared_human_task.h
 * @brief 任务与行为模型；任务按 ID 引用全局行为数组。
 */

enum {
    DOMINO_HUMAN_TASK_CHANNEL_PRIMARY = 0,  ///< 同轮启动按主通道优先；自主睡眠选择主通道。
    DOMINO_HUMAN_TASK_CHANNEL_SECONDARY_1 = 1,
    DOMINO_HUMAN_TASK_CHANNEL_SECONDARY_2 = 2,
    DOMINO_HUMAN_TASK_CHANNEL_COUNT = 3,
    DOMINO_HUMAN_TASK_COUNT_MAX = 32,
    DOMINO_TASK_BEHAVIOR_COUNT_MAX = UINT8_MAX,
};

/** @brief 任务类型：人物打算完成哪件事；一个任务可以包含多个行为，例如上班包含通勤、工作和午餐。
 * @note 类型只描述业务含义，具体规划和执行逻辑需由对应任务实现。
 */
typedef enum {
    DOMINO_TASK_TYPE_NONE = 0,       ///< 尚未指定任务类型，不能作为有效任务提交。
    DOMINO_TASK_TYPE_WORK,           ///< 上班：围绕一次工作安排组织通勤、工作等步骤。
    DOMINO_TASK_TYPE_SCHOOL,         ///< 上学：围绕一次上学安排组织通勤、学习等步骤。
    DOMINO_TASK_TYPE_ENTERTAINMENT,  ///< 娱乐：安排看电影、玩游戏等休闲活动。
    DOMINO_TASK_TYPE_SHOPPING,       ///< 购物：前往购物地点并完成购买。
    DOMINO_TASK_TYPE_EXERCISE,       ///< 运动：安排跑步、健身等锻炼活动。
    DOMINO_TASK_TYPE_MEDICAL,        ///< 就医：前往医疗地点并接受诊治。
    DOMINO_TASK_TYPE_VISIT,          ///< 拜访：前往他人所在地点并进行交流。
    DOMINO_TASK_TYPE_TRAVEL,         ///< 旅游：安排出行、参观等活动，可包含过夜等行为。
    DOMINO_TASK_TYPE_PARADE,         ///< 游行：参加一次集体游行活动。
    DOMINO_TASK_TYPE_SLEEP,          ///< 睡眠：以睡觉为核心，也可包含晚餐、洗漱和早餐。
    DOMINO_TASK_TYPE_PHONE_CALL,     ///< 通话：安排一次电话交流。
    DOMINO_TASK_TYPE_MESSAGE,        ///< 消息处理：安排一次接收、阅读或发送消息的操作。
    DOMINO_TASK_TYPE_COUNT,          ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_TYPE;

/** @brief 行为类型：任务中的一个具体步骤；例如上班任务中的移动和工作是两种行为。 */
typedef enum {
    DOMINO_BEHAVIOR_TYPE_NONE = 0,    ///< 尚未指定行为类型，不能作为有效行为使用。
    DOMINO_BEHAVIOR_TYPE_MOVE,        ///< 移动：从当前位置前往目标地点。
    DOMINO_BEHAVIOR_TYPE_EAT,         ///< 吃饭：执行一次进餐，早餐、午餐和晚餐共用此类型。
    DOMINO_BEHAVIOR_TYPE_WASH,        ///< 洗漱：执行一次清洁，睡前和起床洗漱共用此类型。
    DOMINO_BEHAVIOR_TYPE_WORK,        ///< 工作：实际进行劳动的步骤，不包含通勤等其他步骤。
    DOMINO_BEHAVIOR_TYPE_STUDY,       ///< 学习：实际上课或学习的步骤。
    DOMINO_BEHAVIOR_TYPE_SHOP,        ///< 购物：实际选购、付款等购买过程。
    DOMINO_BEHAVIOR_TYPE_EXERCISE,    ///< 运动：实际跑步、训练等，不是去健身地点的通勤。
    DOMINO_BEHAVIOR_TYPE_TREAT,       ///< 看病：实际接受检查或治疗的步骤。
    DOMINO_BEHAVIOR_TYPE_CHAT,        ///< 聊天：面对面交流。
    DOMINO_BEHAVIOR_TYPE_SIGHTSEE,    ///< 参观：在景点等地点游览。
    DOMINO_BEHAVIOR_TYPE_PARADE,      ///< 游行：实际参与集体游行的步骤。
    DOMINO_BEHAVIOR_TYPE_SLEEP,       ///< 睡觉：实际累计有效睡眠时长，不包含餐食和洗漱。
    DOMINO_BEHAVIOR_TYPE_ENTERTAIN,   ///< 娱乐：实际看电影、玩游戏等。
    DOMINO_BEHAVIOR_TYPE_PHONE_CALL,  ///< 通话：通过电话进行同步交流。
    DOMINO_BEHAVIOR_TYPE_MESSAGE,     ///< 消息处理：执行接收、阅读或发送之一；接收不等于已读或回复。
    DOMINO_BEHAVIOR_TYPE_COUNT,       ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_BEHAVIOR_TYPE;

/** @brief 执行状态只用于已建立执行部分的任务。 */
typedef enum { DOMINO_TASK_STATUS_OPEN, DOMINO_TASK_STATUS_SUCCEEDED, DOMINO_TASK_STATUS_FAILED, DOMINO_TASK_STATUS_CANCELLED } DOMINO_TASK_STATUS;
typedef enum {
    DOMINO_BEHAVIOR_STATUS_ACTIVE,
    DOMINO_BEHAVIOR_STATUS_WAITING,
    DOMINO_BEHAVIOR_STATUS_SUCCEEDED,
    DOMINO_BEHAVIOR_STATUS_FAILED,
    DOMINO_BEHAVIOR_STATUS_CANCELLED
} DOMINO_BEHAVIOR_STATUS;

enum {
    DOMINO_HUMAN_MOTIVATION_COUNT_MAX = 8,
    DOMINO_MOTIVATION_HISTORY_MAX = 10,
    DOMINO_TASK_DEPENDENCY_MAX = 4,
    DOMINO_HUMAN_DISPLAY_NAME_MAX = 63,
    DOMINO_HUMAN_WORLD_COUNT_MAX = 10000,
    DOMINO_MOTIVATION_POOL_MAX = 80000,
    DOMINO_TASK_POOL_MAX = 320000,
    DOMINO_BEHAVIOR_POOL_MAX = 1000000,
    DOMINO_BEHAVIOR_PLAN_POOL_MAX = 1000000,
};

/** @brief 有业务含义的绝对检查时刻；0 和 UINT32_MAX 均不是空值。 */
typedef struct DominoHumanCheckTime {
    domino_runtime_ms_t at_ms;
    bool valid;
} DominoHumanCheckTime;
typedef enum { DOMINO_MOTIVATION_RECOVER_ENERGY = 1 } DominoMotivationType;
typedef enum { DOMINO_MOTIVATION_VALID, DOMINO_MOTIVATION_ACHIEVED, DOMINO_MOTIVATION_FAILED, DOMINO_MOTIVATION_REVOKED } DominoMotivationStatus;
typedef struct DominoMotivationImplementation {
    domino_task_id_t task_id;
    domino_runtime_ms_t started_at_ms;
    domino_runtime_ms_t ended_at_ms;
} DominoMotivationImplementation;

/** @brief 维持型需求的轮次在接受时推进；closed_round 是独立于滚动历史的长期去重事实。 */
typedef struct DominoMotivation {
    uint64_t implementation_count;
    uint64_t work_round;
    uint64_t closed_round;
    DominoMotivationImplementation history[DOMINO_MOTIVATION_HISTORY_MAX];
    DominoHumanCheckTime retry;
    domino_motivation_id_t motivation_id;
    domino_human_id_t owner_human_id;
    DominoMotivationType type;
    DominoMotivationStatus status;
    uint32_t last_failure_reason;
    uint8_t history_count;
    bool needs_action;
    bool reassess;
    bool failed_work;
} DominoMotivation;

/** @brief 尚未运行的值，没有正式行为 ID；时长不由预约时间差推导。 */
typedef struct DominoBehaviorPlan {
    domino_runtime_ms_t duration_ms;
    DOMINO_BEHAVIOR_TYPE type;
} DominoBehaviorPlan;

enum { DOMINO_DEPENDENCY_START = 1, DOMINO_DEPENDENCY_CONTINUE = 2, DOMINO_DEPENDENCY_COMPLETE = 4 };
typedef enum { DOMINO_DEPENDENCY_SUCCEEDED, DOMINO_DEPENDENCY_ACTIVE } DominoDependencyCondition;
typedef struct DominoTaskDependency {
    domino_task_id_t task_id;
    DominoDependencyCondition condition;
    uint8_t stages;
    uint8_t resolved_result;  ///< 0 引用未解除；1 已满足；2 来源终结而未满足。
} DominoTaskDependency;

/** @brief 正式行为只在实际开始时分配；终态已与任务事实同步归并。 */
typedef struct DominoBehavior {
    DominoBehaviorPlan plan;
    domino_behavior_id_t behavior_id;
    domino_task_id_t task_id;
    domino_behavior_id_t next_record_id;
    domino_runtime_ms_t started_at_ms;
    domino_runtime_ms_t ended_at_ms;
    domino_runtime_ms_t active_elapsed_ms;
    domino_runtime_ms_t effect_phase_ms;
    DOMINO_BEHAVIOR_STATUS status;
} DominoBehavior;

/** @brief 可选执行部分；has_execution 为真后永久存在，实际开始时刻 0 合法。 */
typedef struct DominoTaskExecution {
    uint64_t successful_behavior_count;
    domino_runtime_ms_t completed_active_ms;
    domino_behavior_id_t current_behavior_id;
    domino_runtime_ms_t started_at_ms;
    domino_runtime_ms_t ended_at_ms;
    DOMINO_TASK_STATUS status;
    uint32_t last_reason;
} DominoTaskExecution;

/** @brief 同一 task_id 从接受贯穿执行与回收。计划块由本任务独占，不借用候选。 */
typedef struct DominoTask {
    uint64_t work_round;
    uint64_t plan_version;         ///< 仅运行期，未来序列变化才递增。
    uint64_t arrangement_version;  ///< 仅运行期，任务安排或依赖事实变化时递增。
    DominoBehaviorPlan* plans;
    DominoTaskExecution execution;
    DominoTaskDependency dependencies[DOMINO_TASK_DEPENDENCY_MAX];
    DominoHumanCheckTime retry;
    DominoHumanCheckTime not_before;
    DominoHumanCheckTime start_deadline;
    DominoHumanCheckTime finish_deadline;
    domino_task_id_t task_id;
    domino_human_id_t owner_human_id;
    domino_motivation_id_t motivation_id;
    domino_behavior_id_t first_record_id;
    DOMINO_TASK_TYPE type;
    uint16_t plan_count;
    uint16_t record_count;
    uint8_t channel_index;
    uint8_t dependency_count;
    uint8_t target_energy;
    bool has_execution;
    bool waiting_condition;
    bool finish_inclusive;
} DominoTask;

#endif
