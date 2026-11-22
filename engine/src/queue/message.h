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
 * @brief 引擎内部队列消息：计时登记、三类人物规划的请求／结果、规划候选扁平块和人物执行器收件消息（宪法 3.6）。
 * @note 单类型队列直接保存对应结构；多类型队列用 id + union payload + type。内部消息都不跨会话保存，
 * 重新加载后由持久化的人物、动机、任务和行为事实重建。
 */

/** @brief GameTimer 登记 ID，本次 Init 内唯一；不是时间值。 */
typedef uint64_t domino_game_timer_id_t;

/** @brief 计时登记记录：命令队列、最小堆和到期通知按值复用同一结构。
 * @note game_timer_id 由命令队列生成；到期时本记录嵌入 DominoHumanInboxMessage，外层 id 另由收件队列生成。
 * 不得把本记录原样再次入队，否则首字段的计时器身份会被覆盖。
 */
typedef struct DominoGameTimerRequestMessage {
    domino_game_timer_id_t game_timer_id;  ///< 首字段，由命令队列写入，用作取消凭据。
    DominoThreadQueue* event_queue_ptr;    ///< 到期通知的目标收件队列，元素为 DominoHumanInboxMessage。
    uint64_t target_id;                    ///< 业务目标 ID，原样传递；GameTimer 不解释其含义。
    domino_runtime_ms_t due_ms;            ///< now_ms >= due_ms 时到期。
    uint8_t event_type;
    bool cancelled_flag;  ///< 由 Cancel 标记。
} DominoGameTimerRequestMessage;

static_assert(offsetof(DominoGameTimerRequestMessage, game_timer_id) == 0,
              "DominoGameTimerRequestMessage game_timer_id must be first for queue ID writeback");
static_assert(sizeof(DominoGameTimerRequestMessage) == 32, "DominoGameTimerRequestMessage size must be 32 bytes");

/* ---------------------------------------------------------------------------------------------------------------- */
/* 三类规划器：类别、优先级、触发范围                                                                                 */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 规划器类别；每类一个线程，各有高／普通优先级的请求队列和结果队列。 */
typedef enum {
    DOMINO_HUMAN_PLANNER_MOTIVATION = 0,
    DOMINO_HUMAN_PLANNER_TASK,
    DOMINO_HUMAN_PLANNER_BEHAVIOR,
    DOMINO_HUMAN_PLANNER_COUNT,
} DOMINO_HUMAN_PLANNER_KIND;

/** @brief 规划优先级：只决定处理先后，不授予抢占运行任务或绕过前置条件的权限。 */
typedef enum {
    DOMINO_HUMAN_PLAN_PRIORITY_HIGH = 0,
    DOMINO_HUMAN_PLAN_PRIORITY_NORMAL,
    DOMINO_HUMAN_PLAN_PRIORITY_COUNT,
} DOMINO_HUMAN_PLAN_PRIORITY;

/** @brief 规划请求的触发范围，只记在人物请求槽中，不进入请求消息。 */
typedef enum {
    DOMINO_HUMAN_PLAN_SCOPE_NONE = 0,
    DOMINO_HUMAN_PLAN_SCOPE_HUMAN,  ///< 以人物为范围（动机和任务规划），scope_id 不使用。
    DOMINO_HUMAN_PLAN_SCOPE_TASK,   ///< 已接受任务的行为规划；scope_id 为 task_id。
    DOMINO_HUMAN_PLAN_SCOPE_COUNT,
} DOMINO_HUMAN_PLAN_SCOPE;

/** @brief 规划请求消息；类别和优先级由所在队列决定。完整请求身份为 human_id + 类别 + 优先级 + request_id。 */
typedef struct DominoHumanPlanRequestMessage {
    uint64_t request_id;  ///< 首字段，由请求队列生成。
    domino_human_id_t human_id;
} DominoHumanPlanRequestMessage;

static_assert(offsetof(DominoHumanPlanRequestMessage, request_id) == 0,
              "DominoHumanPlanRequestMessage request_id must be first for queue ID writeback");

/** @brief 规划结果分类。 */
typedef enum {
    DOMINO_HUMAN_PLAN_OUTCOME_NONE = 0,
    DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE,  ///< 携带候选扁平块。
    DOMINO_HUMAN_PLAN_OUTCOME_NO_CHANGE,  ///< 无需调整。
    DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN,    ///< 暂时无方案，含计算预算或候选内存预算耗尽。
    DOMINO_HUMAN_PLAN_OUTCOME_COUNT,
} DOMINO_HUMAN_PLAN_OUTCOME;

/** @brief 规划结果消息，送入与原请求同优先级的结果队列。
 * @note candidate_ptr 指向独占的候选块：入队成功后归执行器，无论采用、拒绝还是作废，都在处理或清理时整块释放一次。
 * 动机和任务规划器随结果给出下次规划时刻或“无”；行为规划器恒为“无”。
 */
typedef struct DominoHumanPlanResultMessage {
    uint64_t id;                     ///< 结果队列回写的消息 ID，与 request_id 独立。
    uint64_t request_id;             ///< 原请求身份，不被结果队列 ID 覆盖。
    uint64_t enqueued_monotonic_ns;  ///< 投递时的单调纳秒，只用于性能统计。
    void* candidate_ptr;             ///< OUTCOME_CANDIDATE 时非空。
    domino_human_id_t human_id;
    domino_runtime_ms_t next_plan_at_ms;  ///< 下次规划时刻，晚于规划时刻；next_plan_valid_flag 为真时有效。
    uint8_t planner_kind;                 ///< DOMINO_HUMAN_PLANNER_KIND，冗余保存供诊断。
    uint8_t priority;                     ///< DOMINO_HUMAN_PLAN_PRIORITY。
    uint8_t outcome;                      ///< DOMINO_HUMAN_PLAN_OUTCOME。
    bool next_plan_valid_flag;            ///< 为假表示“无”。
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

/** @brief 候选公共头，条目区和步骤区按 8 字节对齐紧随其后。
 * @note 前 required_count 个条目必须一起接受，其余为按顺序尝试的可选项。
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

/** @brief 动机候选条目；动机的建立与终结允许信息差（宪法第六章），不带 expected_* 锚点。
 * @note TERMINATE 的目标已不存在、不属于该人物或已不是 ACTIVE 时，执行器忽略该条目，重复终结没有副作用。
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

/** @brief 任务候选条目；新任务只归属一个有效动机。
 * @note 回应外部信息的新任务总放在待执行队首并带让位请求，接受即承接该外部信息；其他新任务按 insert_index 入队。
 */
typedef struct DominoTaskCandidateEntry {
    uint64_t expected_queue_version;  ///< 指定插入位置时的身份锚点：读取时的待执行队列版本。
    domino_task_id_t task_id;         ///< CANCEL 目标。
    domino_motivation_id_t motivation_id;
    uint32_t work_source_id;
    uint32_t work_round;
    uint32_t expected_motivation_round;  ///< 睡眠任务的身份锚点：读取时动机的需求轮次。
    domino_runtime_ms_t deadline_ms;
    uint8_t op;  ///< DOMINO_TASK_CANDIDATE_OP。
    uint8_t type;
    uint8_t insert_index;  ///< 队列插入位置，DOMINO_PLAN_CANDIDATE_INSERT_APPEND 为末尾。
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

/** @brief 行为候选条目：替换任务的未来步骤（REPLACE），或为运行任务插入回应外部信息的辅助步骤（INSERT）。
 * @note 本条目的步骤为步骤区 [step_index, step_index + step_count)，step_seq 不使用；依赖来源带 DOMINO_PLAN_CANDIDATE_LOCAL_REF
 * 时指向本条目更早的步骤，否则为本任务当前行为的步骤序号。REPLACE 的 step_count 为 0 表示清空自身步骤。
 * 身份锚点：REPLACE 核对计划版本，depends_current_flag 时还核对各通道当前行为；INSERT 核对外部信息轮次。
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
/* 人物执行器收件箱                                                                                                   */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 人物执行器收件消息：唯一类型是 GameTimer 到期通知；规划结果走独立的结果队列。
 * @note 不直接复用 DominoGameTimerRequestMessage：收件队列回写首字段，会覆盖计时器身份。载荷按值保存，无需释放。
 */
typedef struct DominoHumanInboxMessage {
    uint64_t id;                               ///< 收件队列回写的消息 ID，与计时记录中的业务 ID 无关。
    DominoGameTimerRequestMessage game_timer;  ///< 按值保存的原登记记录。
} DominoHumanInboxMessage;

static_assert(offsetof(DominoHumanInboxMessage, id) == 0, "DominoHumanInboxMessage id must be first for queue ID writeback");

#endif
