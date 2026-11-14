#ifndef DOMINO_ENGINE_QUEUE_MESSAGE_H
#define DOMINO_ENGINE_QUEUE_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_engine.h"
#include "domino_shared_human_task.h"
#include "domino_shared_queue.h"
#include "domino_shared_types.h"

/** @file message.h
 * @brief 引擎内部队列消息及其复用载荷；按计时登记、人物规划和人物执行用途命名。
 * @note 单类型队列直接保存相应请求结构；多类型队列使用 id + union payload + type。
 * 引擎对外消息定义于 domino_engine.h。
 * 内部消息不跨会话保存：计时登记／通知由持久化人物需求及任务／行为进度重建，未提交规划由人物、动机及已接受任务事实重新生成；
 * 已提交任务及行为保存正式身份和进度，加载时不重放旧请求或候选，不重复结算已生效结果。
 */

/** @brief 64 位登记 ID，不是时间值；业务毫秒统一使用 32 位 domino_runtime_ms_t。 */
typedef uint64_t domino_game_timer_id_t;

/** @brief 计时登记请求；命令队列直接保存本结构，最小堆和到期通知复用同一记录。
 * @note 命令入队生成 game_timer_id；到期通知将本记录按值嵌入 DominoHumanExecutionMessage，外层 id 接收业务队列的新编号。
 * 不得将本记录原样二次入队，以免首字段的稳定计时器身份被覆盖。
 */
typedef struct DominoGameTimerRequestMessage {
    domino_game_timer_id_t game_timer_id;  ///< 必须为首字段，由登记命令队列写入；本次 Init 生命周期内的稳定取消凭据。
    DominoThreadQueue* event_queue_ptr;    ///< 业务方拥有的收件队列，元素类型为 DominoHumanExecutionMessage。
    uint64_t data_id;                      ///< 业务标识，原样按值传递；GameTimer 不解释其含义或持有业务对象。
    domino_runtime_ms_t due_ms;            ///< 到期时刻：累计未冻结现实时间达到此毫秒数时到期，即 now_ms >= due_ms。
    uint8_t event_type;
    bool cancelled_flag;  ///< 取消标志，登记接口初始化为 false，由 Cancel 按请求 ID 标记。
} DominoGameTimerRequestMessage;

static_assert(offsetof(DominoGameTimerRequestMessage, game_timer_id) == 0,
              "DominoGameTimerRequestMessage game_timer_id must be first for queue ID writeback");
static_assert(sizeof(DominoGameTimerRequestMessage) == 32, "DominoGameTimerRequestMessage size must be 32 bytes");

/** @brief 类别/优先级由队列和固定请求槽推导。 */
typedef enum { DOMINO_PLANNER_MOTIVATION, DOMINO_PLANNER_TASK, DOMINO_PLANNER_BEHAVIOR, DOMINO_PLANNER_COUNT } DominoPlannerKind;
typedef enum { DOMINO_PLAN_HIGH, DOMINO_PLAN_NORMAL, DOMINO_PLAN_PRIORITY_COUNT } DominoPlanPriority;
typedef struct DominoPlanScope {
    uint32_t object_id;
    bool accepted_task;
} DominoPlanScope;
typedef struct DominoPlanRequestState {
    uint64_t request_id;
    DominoPlanScope trigger_scope;
    domino_runtime_ms_t deadline_ms;
    DominoPlanPriority request_priority;
} DominoPlanRequestState;
typedef struct DominoHumanPlanRequestMessage {
    uint64_t request_id;
    uint64_t queued_at_ns;  ///< 性能采样，与业务资格分开，不持久化。
    domino_human_id_t human_id;
} DominoHumanPlanRequestMessage;

enum { DOMINO_PLAN_ITEM_MAX = 8, DOMINO_PLAN_STEP_MAX = 255, DOMINO_PLAN_BYTES_MAX = 65536, DOMINO_PLAN_LOCAL_NONE = UINT16_MAX };
typedef enum { DOMINO_PLAN_CANDIDATE, DOMINO_PLAN_UNCHANGED, DOMINO_PLAN_UNAVAILABLE } DominoPlanOutcome;
/** @brief 每类一块连续只读载荷；偏移均相对于块首，构造按实际大小分配。 */
typedef struct DominoPlanCandidate {
    uint32_t bytes;
    uint16_t item_count;
    uint16_t step_count;
} DominoPlanCandidate;
typedef struct DominoMotivationSuggestion {
    domino_motivation_id_t motivation_id;  ///< 0 表示新增。
    DominoMotivationType type;
    bool needs_action;
    bool optional;
} DominoMotivationSuggestion;
typedef struct DominoMotivationCandidate {
    DominoPlanCandidate header;
    DominoMotivationSuggestion items[];
} DominoMotivationCandidate;
typedef struct DominoTaskSuggestion {
    uint64_t expected_arrangement_version;
    uint64_t expected_queue_version;
    uint64_t work_round;
    DominoTaskDependency dependencies[DOMINO_TASK_DEPENDENCY_MAX];
    uint16_t dependency_local[DOMINO_TASK_DEPENDENCY_MAX];
    DominoHumanCheckTime not_before, start_deadline, finish_deadline;
    domino_task_id_t task_id;  ///< 0 表示新增；其他值只调整原记录的许可安排。
    domino_motivation_id_t motivation_id;
    DOMINO_TASK_TYPE type;
    uint8_t channel_index;
    uint8_t queue_position;
    uint8_t target_energy;
    uint8_t dependency_count;
    bool optional;
    bool finish_inclusive;
    bool yield_current;
    bool cancel;
} DominoTaskSuggestion;
typedef struct DominoTaskCandidate {
    DominoPlanCandidate header;
    DominoTaskSuggestion items[];
} DominoTaskCandidate;
typedef struct DominoBehaviorSuggestion {
    uint64_t expected_plan_version;
    uint32_t steps_offset;
    domino_task_id_t task_id;
    domino_behavior_id_t expected_behavior_id;
    domino_runtime_ms_t adjusted_duration_ms;
    domino_runtime_ms_t expected_duration_ms;  ///< 调整当前行为时依赖的旧参数，进度自然增加不使其失效。
    uint16_t step_count;
    bool replace_plans;
    bool depends_on_behavior;
    bool requires_unstarted;
    bool adjust_current;
    bool optional;
} DominoBehaviorSuggestion;
typedef struct DominoBehaviorCandidate {
    DominoPlanCandidate header;
    DominoBehaviorSuggestion items[];
} DominoBehaviorCandidate;
/** @brief 结果沿原优先级进入独立队列。候选成功投递后整块归执行器。 */
typedef struct DominoHumanPlanResultMessage {
    uint64_t id;
    uint64_t request_id;
    uint64_t queued_at_ns;
    DominoPlanCandidate* candidate;
    domino_human_id_t human_id;
    DominoPlannerKind planner_kind;
    DominoPlanPriority request_priority;
    DominoPlanOutcome outcome;
} DominoHumanPlanResultMessage;
typedef struct DominoHumanConditionUpdate {
    uint64_t queued_at_ns;
    domino_human_id_t human_id;
    bool sleep_allowed;
} DominoHumanConditionUpdate;
typedef struct DominoHumanEnergyUpdate {
    uint64_t queued_at_ns;
    domino_human_id_t human_id;
    uint8_t energy;
} DominoHumanEnergyUpdate;
typedef struct DominoHumanNameUpdate {
    uint64_t client_id;
    DominoNpcRequest request;
} DominoHumanNameUpdate;
typedef enum {
    DOMINO_HUMAN_EXECUTION_MESSAGE_NONE,
    DOMINO_HUMAN_EXECUTION_MESSAGE_GAME_TIMER,
    DOMINO_HUMAN_EXECUTION_MESSAGE_SLEEP_CONDITION,
    DOMINO_HUMAN_EXECUTION_MESSAGE_ENERGY,
    DOMINO_HUMAN_EXECUTION_MESSAGE_NAME,
} DominoHumanExecutionMessageType;
typedef struct DominoHumanExecutionMessage {
    uint64_t id;
    union {
        DominoGameTimerRequestMessage game_timer;
        DominoHumanConditionUpdate condition;
        DominoHumanEnergyUpdate energy;
        DominoHumanNameUpdate name;
    } payload;
    DominoHumanExecutionMessageType type;
} DominoHumanExecutionMessage;
static_assert(offsetof(DominoHumanExecutionMessage, id) == 0, "queue ID first");
static_assert(offsetof(DominoHumanPlanResultMessage, id) == 0, "queue ID first");
static_assert(offsetof(DominoHumanPlanRequestMessage, request_id) == 0, "queue ID first");
#endif
