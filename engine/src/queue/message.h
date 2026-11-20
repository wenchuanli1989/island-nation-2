#ifndef DOMINO_ENGINE_QUEUE_MESSAGE_H
#define DOMINO_ENGINE_QUEUE_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_human.h"
#include "domino_shared_human_task.h"
#include "domino_shared_queue.h"
#include "domino_shared_types.h"

/** @file message.h
 * @brief 引擎内部队列消息、相关枚举及专用载荷：计时登记、三类人物规划请求／结果、规划候选扁平块和人物执行器收件消息。
 * @note 单类型队列直接保存相应结构；多类型队列使用 id + union payload + type。引擎对外消息定义于 domino_engine_protocol.h。
 * 内部消息不跨会话保存：计时登记、规划请求、候选、待办、待规划标志及下次规划时刻由持久化的人物、动机、任务和行为事实重建；
 * 已接受的动机、任务、行为计划和执行记录保存正式身份与进度，加载时不重放旧请求或候选，不重复结算已生效结果。
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

/* ---------------------------------------------------------------------------------------------------------------- */
/* 三类规划器：类别、优先级、触发范围                                                                                 */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 规划器类别；每类一个线程和高／普通请求、结果四条队列。 */
typedef enum {
    DOMINO_HUMAN_PLANNER_MOTIVATION = 0,
    DOMINO_HUMAN_PLANNER_TASK,
    DOMINO_HUMAN_PLANNER_BEHAVIOR,
    DOMINO_HUMAN_PLANNER_COUNT,
} DOMINO_HUMAN_PLANNER_KIND;

/** @brief 规划优先级；只决定计算和结果获得处理机会的先后，不授予抢占运行任务或绕过前置条件的权限。 */
typedef enum {
    DOMINO_HUMAN_PLAN_PRIORITY_HIGH = 0,
    DOMINO_HUMAN_PLAN_PRIORITY_NORMAL,
    DOMINO_HUMAN_PLAN_PRIORITY_COUNT,
} DOMINO_HUMAN_PLAN_PRIORITY;

/** @brief 有效请求记录的触发范围类别，只保存在人物请求槽中，不进入请求消息。 */
typedef enum {
    DOMINO_HUMAN_PLAN_SCOPE_NONE = 0,
    DOMINO_HUMAN_PLAN_SCOPE_HUMAN,  ///< 以人物为范围：动机规划和任务规划；人物由所属记录确定，scope_id 不使用。
    DOMINO_HUMAN_PLAN_SCOPE_TASK,   ///< 已接受任务的行为规划；scope_id 为 task_id。
    DOMINO_HUMAN_PLAN_SCOPE_COUNT,
} DOMINO_HUMAN_PLAN_SCOPE;

/** @brief 六条规划请求队列共用的请求消息；类别和优先级由所属队列确定，触发范围只在人物请求槽中。
 * @note 首字段为请求队列生成的 request_id；完整请求身份为 human_id + 类别 + 优先级 + request_id。
 */
typedef struct DominoHumanPlanRequestMessage {
    uint64_t request_id;  ///< 必须为首字段，由请求队列生成。
    domino_human_id_t human_id;
} DominoHumanPlanRequestMessage;

static_assert(offsetof(DominoHumanPlanRequestMessage, request_id) == 0,
              "DominoHumanPlanRequestMessage request_id must be first for queue ID writeback");

/** @brief 规划器回传的明确结果。 */
typedef enum {
    DOMINO_HUMAN_PLAN_OUTCOME_NONE = 0,
    DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE,  ///< 携带候选扁平块。
    DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE,  ///< 无需调整。
    DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN,    ///< 暂时无方案，包括计算预算或候选内存预算耗尽。
    DOMINO_HUMAN_PLAN_OUTCOME_COUNT,
} DOMINO_HUMAN_PLAN_OUTCOME;

/** @brief 六条规划结果队列共用的结果消息；结果沿用原请求的优先级送入对应结果队列。
 * @note candidate_ptr 指向独占的候选扁平块，成功入队后整块交给执行器，失败仍归规划线程；
 * 采用、拒绝、旧或重复及停止后留队的结果都在处理或清理结束时整块释放一次。
 * 动机和任务规划器随每种结果给出该人物这一类的下次规划时刻或“无”；行为规划恒为无效。
 */
typedef struct DominoHumanPlanResultMessage {
    uint64_t id;                ///< 结果队列回写的消息 ID，与 request_id 独立。
    uint64_t request_id;        ///< 原请求身份，不被结果队列 ID 覆盖。
    uint64_t enqueued_mono_ns;  ///< 性能采样：投递时的单调纳秒，不参与业务判断。
    void* candidate_ptr;        ///< OUTCOME_CANDIDATE 时非空。
    domino_human_id_t human_id;
    domino_runtime_ms_t next_plan_at_ms;  ///< 下次规划时刻，晚于规划时刻；仅 next_plan_valid_flag 为真时有效。
    uint8_t planner_kind;                 ///< DOMINO_HUMAN_PLANNER_KIND，冗余保存供载荷释放及诊断。
    uint8_t priority;                     ///< DOMINO_HUMAN_PLAN_PRIORITY。
    uint8_t outcome;                      ///< DOMINO_HUMAN_PLAN_OUTCOME。
    bool next_plan_valid_flag;            ///< 为假表示“无”：规划器不要求以后再看。
} DominoHumanPlanResultMessage;

static_assert(offsetof(DominoHumanPlanResultMessage, id) == 0, "DominoHumanPlanResultMessage id must be first for queue ID writeback");

/* ---------------------------------------------------------------------------------------------------------------- */
/* 候选扁平块：一份候选独占一块连续内存，块内关系只用局部索引和偏移                                                    */
/* ---------------------------------------------------------------------------------------------------------------- */

enum {
    DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX = 8,         ///< 单份候选的新增／调整建议上限。
    DOMINO_PLAN_CANDIDATE_STEP_COUNT_MAX = UINT8_MAX,  ///< 单份行为候选的步骤总数上限。
    DOMINO_PLAN_CANDIDATE_BYTES_MAX = 64U * 1024U,     ///< 单份候选含对齐填充的总字节上限，由 candidate.c 静态断言保证。
    DOMINO_PLAN_CANDIDATE_INSERT_APPEND = UINT8_MAX,   ///< 新任务追加到待执行队列末尾。
};

/** @brief 行为依赖来源高位：置位时低位为同一候选条目内更早步骤的局部序号，否则为本任务当前行为的步骤序号。 */
#define DOMINO_PLAN_CANDIDATE_LOCAL_REF UINT32_C(0x80000000)

/** @brief 候选公共头；条目区和步骤区按对齐偏移紧随其后。
 * @note 前 required_count 个条目为必须一起接受的必要部分，其余为按顺序尝试的可选项。
 */
typedef struct DominoPlanCandidateHeader {
    uint32_t total_size;    ///< 含头部和对齐填充的总字节数。
    uint32_t entry_offset;  ///< 条目区相对块首的偏移。
    uint32_t step_offset;   ///< 步骤区相对块首的偏移，仅行为候选使用。
    uint16_t step_count;    ///< 步骤区项数。
    uint8_t planner_kind;   ///< DOMINO_HUMAN_PLANNER_KIND。
    uint8_t entry_count;
    uint8_t required_count;
    uint8_t reserved[7];
} DominoPlanCandidateHeader;

/** @brief 动机候选操作；无需调整时规划器直接返回 OUTCOME_NO_CHANGE，不构造候选。 */
typedef enum {
    DOMINO_MOTIVATION_CANDIDATE_NONE = 0,
    DOMINO_MOTIVATION_CANDIDATE_CREATE,     ///< 新建动机；同类型已有有效动机时复用原动机。
    DOMINO_MOTIVATION_CANDIDATE_TERMINATE,  ///< 终结人物的一项有效动机，写入达成、失败或撤销终态。
} DOMINO_MOTIVATION_CANDIDATE_OP;

/** @brief 动机候选条目；不携带 expected_* 依赖，动机的建立与终结按宪法第六章允许信息差。
 * @note TERMINATE 的目标已不存在、不属于该人物或已不是 ACTIVE 时，执行器忽略该条目，因此重复终结没有副作用。
 */
typedef struct DominoMotivationCandidateEntry {
    domino_motivation_id_t motivation_id;  ///< TERMINATE 目标；CREATE 填 0。
    uint8_t op;                            ///< DOMINO_MOTIVATION_CANDIDATE_OP。
    uint8_t type;                          ///< CREATE 的 DOMINO_MOTIVATION_TYPE；TERMINATE 不使用。
    uint8_t terminal_status;               ///< TERMINATE 的终态：ACHIEVED、FAILED 或 REVOKED；CREATE 不使用。
    uint8_t reserved;
} DominoMotivationCandidateEntry;

/** @brief 任务候选操作。 */
typedef enum {
    DOMINO_TASK_CANDIDATE_NONE = 0,
    DOMINO_TASK_CANDIDATE_CREATE,  ///< 新任务计划；接受时分配 task_id。
    DOMINO_TASK_CANDIDATE_CANCEL,  ///< 合法撤销未启动任务或取消执行中任务。
} DOMINO_TASK_CANDIDATE_OP;

/** @brief 任务候选条目；每个新任务只归属一个动机，所属动机须为该人物的有效动机。
 * @note 来源为外部信息的新任务回应人物当前尚未承接的外部信息：总是放在待执行队列队首并带让位请求，接受后外部信息即被承接；
 * 其他新任务按 insert_index 进入待执行队列。被拒绝或省略的新任务不建立记录。
 */
typedef struct DominoTaskCandidateEntry {
    uint64_t expected_queue_version;  ///< 指定插入位置时依赖读取时的待执行队列版本；追加或回应外部信息时不使用。
    domino_task_id_t task_id;         ///< CANCEL 目标。
    domino_motivation_id_t motivation_id;
    uint32_t work_source_id;
    uint32_t work_round;
    uint32_t expected_motivation_round;  ///< 睡眠新工作的实际依赖：读取时动机的最近已接受轮次。
    domino_runtime_ms_t deadline_ms;
    uint8_t op;  ///< DOMINO_TASK_CANDIDATE_OP。
    uint8_t type;
    uint8_t insert_index;  ///< 队列插入位置，DOMINO_PLAN_CANDIDATE_INSERT_APPEND 表示末尾。
    uint8_t target_value;
    uint8_t work_source_kind;
    uint8_t task_flags;  ///< 仅 DEADLINE_VALID／DEADLINE_INCLUSIVE 有效。
    uint8_t reserved[2];
} DominoTaskCandidateEntry;

/** @brief 行为候选操作。 */
typedef enum {
    DOMINO_BEHAVIOR_CANDIDATE_NONE = 0,
    DOMINO_BEHAVIOR_CANDIDATE_REPLACE,  ///< 整体替换任务自身的未来步骤；已插入但未启动的辅助步骤保留在各自通道前部。
    DOMINO_BEHAVIOR_CANDIDATE_INSERT,   ///< 运行任务接受插入：为待处理外部信息加入只用辅助通道、没有依赖的辅助步骤。
} DOMINO_BEHAVIOR_CANDIDATE_OP;

/** @brief 行为候选条目：替换某任务的未来步骤，或为运行任务插入回应外部信息的辅助步骤。
 * @note 步骤区中本条目的步骤为 DominoBehaviorPlanItem，step_seq 不使用；依赖来源带 DOMINO_PLAN_CANDIDATE_LOCAL_REF 高位时
 * 指向本条目更早的步骤，否则为本任务当前行为的步骤序号。REPLACE 的 step_count 为 0 表示显式清空自身步骤。
 * REPLACE 依赖读取时的计划版本；声明依赖当前行为时，各通道当前行为须与读取时一致（0 表示要求该通道没有当前行为）。
 * INSERT 依赖人物外部信息尚未承接且轮次等于读取时的值；不复核运行任务的应激选择。
 */
typedef struct DominoBehaviorCandidateEntry {
    uint64_t expected_plan_version;
    domino_task_id_t task_id;
    domino_behavior_id_t expected_current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_COUNT];
    uint32_t expected_stimulus_round;  ///< INSERT 的身份锚点：读取时人物的外部信息轮次。
    uint16_t step_index;               ///< 步骤区起始序号。
    uint16_t step_count;
    uint8_t op;  ///< DOMINO_BEHAVIOR_CANDIDATE_OP。
    uint8_t depends_current_flag;
    uint8_t reserved[2];
} DominoBehaviorCandidateEntry;

/* ---------------------------------------------------------------------------------------------------------------- */
/* 人物执行器混合收件箱                                                                                               */
/* ---------------------------------------------------------------------------------------------------------------- */

#ifdef DOMINO_PLAYGROUND_MODE
/* 测试验收专用：以下三类人物输入只供 playground 验证睡眠条件、精力变化和外部信息，生产构建不编译；
 * 清理测试代码时，与 human/entry.h、human/entry.c、human/executor.c 中同样标记“测试验收专用”的部分一并删除。
 * 每类输入是独立的场景消息，不经字段表或通用字段写入；执行器先按旧值结算到本次时刻，再修改并在同一边界统一决策。
 */

/** @brief 测试验收专用：睡眠条件输入。 */
typedef struct DominoHumanTestSleepConditionInput {
    domino_human_id_t human_id;
    bool forbidden_flag;  ///< 睡眠执行条件当前不成立为 true。
} DominoHumanTestSleepConditionInput;

/** @brief 测试验收专用：精力输入。 */
typedef struct DominoHumanTestEnergyInput {
    domino_human_id_t human_id;
    uint8_t energy;  ///< 新精力，0..DOMINO_HUMAN_NEED_MAX，入队前已校验。
} DominoHumanTestEnergyInput;

/** @brief 测试验收专用：外部信息输入；来源与类别同次写入。 */
typedef struct DominoHumanTestStimulusInput {
    domino_human_id_t human_id;
    uint32_t source_id;     ///< 外部信息来源，例如来电人物 ID。
    uint8_t stimulus_kind;  ///< DOMINO_HUMAN_STIMULUS_KIND，入队前已校验；NONE 表示撤回尚未承接的外部信息。
} DominoHumanTestStimulusInput;
#endif

/** @brief 人物执行器收件消息的类型，由消费者据此选择有效载荷。 */
typedef enum DominoHumanExecutionMessageType {
    DOMINO_HUMAN_EXECUTION_MESSAGE_NONE = 0,
    DOMINO_HUMAN_EXECUTION_MESSAGE_GAME_TIMER,
#ifdef DOMINO_PLAYGROUND_MODE
    DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_SLEEP_CONDITION,  ///< 测试验收专用：payload.test_sleep_condition。
    DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_ENERGY,           ///< 测试验收专用：payload.test_energy。
    DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_STIMULUS,         ///< 测试验收专用：payload.test_stimulus。
#endif
} DominoHumanExecutionMessageType;

/** @brief 人物执行器的混合业务收件消息：计时通知（playground 另有测试验收专用的人物输入）；规划结果走独立的六条结果队列。
 * @note id 必须位于首字段，供通用队列回写；载荷全部按值保存，无需分配或释放。
 */
typedef struct DominoHumanExecutionMessage {
    uint64_t id;  ///< 所属收件队列回写的消息 ID，与载荷内部的业务 ID 独立。
    union {
        DominoGameTimerRequestMessage game_timer;
#ifdef DOMINO_PLAYGROUND_MODE
        DominoHumanTestSleepConditionInput test_sleep_condition;  ///< 测试验收专用。
        DominoHumanTestEnergyInput test_energy;                   ///< 测试验收专用。
        DominoHumanTestStimulusInput test_stimulus;               ///< 测试验收专用。
#endif
    } payload;
    DominoHumanExecutionMessageType type;
} DominoHumanExecutionMessage;

static_assert(offsetof(DominoHumanExecutionMessage, id) == 0, "DominoHumanExecutionMessage id must be first for queue ID writeback");

#endif
