#ifndef DOMINO_SHARED_HUMAN_TASK_H
#define DOMINO_SHARED_HUMAN_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_types.h"

/** @file domino_shared_human_task.h
 * @brief NPC AI 的动机、任务、行为计划、行为执行记录与外部信息模型。
 * @note 架构为 动机 → 任务 → 行为 → 动画：动机和任务在规划结果被接受时取得正式 ID；任务首次启动时才建立执行部分；
 * 行为计划只保存值，行为在真正启动时才分配正式 ID 并建立执行记录。正式 ID 在同一存档世界内递增分配、永不复用。
 * 每个人物同一时间只有一个运行任务；运行任务最多三个行为并行，主通道运行主行为，两个辅助通道运行辅助行为。
 */

enum {
    DOMINO_BEHAVIOR_CHANNEL_MAIN = 0,   ///< 主通道：运行任务的主行为；任务成功证明只取主行为。
    DOMINO_BEHAVIOR_CHANNEL_AUX_1 = 1,  ///< 辅助通道 1：辅助行为。
    DOMINO_BEHAVIOR_CHANNEL_AUX_2 = 2,  ///< 辅助通道 2：辅助行为。
    DOMINO_BEHAVIOR_CHANNEL_COUNT = 3,
    DOMINO_HUMAN_MOTIVATION_COUNT_MAX = 8,     ///< 每人物最多持有的动机数。
    DOMINO_HUMAN_TASK_COUNT_MAX = 32,          ///< 每人物持有的任务记录数（运行任务与待执行队列合计），接受计划即计入。
    DOMINO_TASK_STEP_COUNT_MAX = UINT8_MAX,    ///< 单任务未来行为计划项与未回收行为记录合计上限。
    DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX = 2,  ///< 单个行为步骤的依赖上限。
    DOMINO_MOTIVATION_HISTORY_COUNT_MAX = 10,  ///< 每个动机保留的最近成功实施明细数。
};

/** @brief 步骤序号上限：最高位留给候选内局部引用，正式序号不超过此值，耗尽致命退出。 */
#define DOMINO_BEHAVIOR_STEP_SEQ_MAX UINT32_C(0x7FFFFFFF)

/** @brief 动机类型：人物希望满足的需求或达成的事情。 */
typedef enum {
    DOMINO_MOTIVATION_TYPE_NONE = 0,
    DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY,   ///< 维持型：恢复并维持精力，可长期驱动多轮睡眠任务。
    DOMINO_MOTIVATION_TYPE_RESPOND_CONTACT,  ///< 维持型：回应来电、消息等联系；第一阶段生产策略不产生。
    DOMINO_MOTIVATION_TYPE_COUNT,
} DOMINO_MOTIVATION_TYPE;

/** @brief 动机生命周期；已达成、不可恢复失败和合法撤销为终态，与“已实施”相互独立。
 * @note 终态只由动机规划器提出、执行器提交；终结后不再承接新工作，也不恢复有效，没有关联任务时回收。
 */
typedef enum {
    DOMINO_MOTIVATION_STATUS_ACTIVE = 0,
    DOMINO_MOTIVATION_STATUS_ACHIEVED,
    DOMINO_MOTIVATION_STATUS_FAILED,
    DOMINO_MOTIVATION_STATUS_REVOKED,
    DOMINO_MOTIVATION_STATUS_COUNT,
} DOMINO_MOTIVATION_STATUS;

/** @brief 任务类型：人物打算完成哪件事；类型与行为类型不一一绑定。 */
typedef enum {
    DOMINO_TASK_TYPE_NONE = 0,
    DOMINO_TASK_TYPE_WORK,
    DOMINO_TASK_TYPE_SCHOOL,
    DOMINO_TASK_TYPE_ENTERTAINMENT,
    DOMINO_TASK_TYPE_SHOPPING,
    DOMINO_TASK_TYPE_EXERCISE,
    DOMINO_TASK_TYPE_MEDICAL,
    DOMINO_TASK_TYPE_VISIT,
    DOMINO_TASK_TYPE_TRAVEL,
    DOMINO_TASK_TYPE_PARADE,
    DOMINO_TASK_TYPE_SLEEP,       ///< 睡眠：精力达到完成条件，且至少一个所属睡眠行为成功结束。
    DOMINO_TASK_TYPE_PHONE_CALL,  ///< 通话：通用计时任务规则，第一阶段生产策略不产生。
    DOMINO_TASK_TYPE_MESSAGE,     ///< 消息处理：通用计时任务规则，第一阶段生产策略不产生。
    DOMINO_TASK_TYPE_COUNT,
} DOMINO_TASK_TYPE;

/** @brief 行为类型：任务中的一个具体步骤。 */
typedef enum {
    DOMINO_BEHAVIOR_TYPE_NONE = 0,
    DOMINO_BEHAVIOR_TYPE_MOVE,
    DOMINO_BEHAVIOR_TYPE_EAT,
    DOMINO_BEHAVIOR_TYPE_WASH,
    DOMINO_BEHAVIOR_TYPE_WORK,  ///< 通用计时行为，可按周期消耗精力。
    DOMINO_BEHAVIOR_TYPE_STUDY,
    DOMINO_BEHAVIOR_TYPE_SHOP,
    DOMINO_BEHAVIOR_TYPE_EXERCISE,
    DOMINO_BEHAVIOR_TYPE_TREAT,
    DOMINO_BEHAVIOR_TYPE_CHAT,
    DOMINO_BEHAVIOR_TYPE_SIGHTSEE,
    DOMINO_BEHAVIOR_TYPE_PARADE,
    DOMINO_BEHAVIOR_TYPE_SLEEP,  ///< 睡觉：按正的目标有效时长累计进度，每 6000ms 有效睡眠恢复 1 点精力。
    DOMINO_BEHAVIOR_TYPE_ENTERTAIN,
    DOMINO_BEHAVIOR_TYPE_PHONE_CALL,  ///< 通用计时行为。
    DOMINO_BEHAVIOR_TYPE_MESSAGE,     ///< 通用计时行为。
    DOMINO_BEHAVIOR_TYPE_COUNT,
} DOMINO_BEHAVIOR_TYPE;

/** @brief 任务执行部分状态；NONE 表示尚无执行部分（任务从未启动），其余值仅在首次启动后使用。
 * @note OPEN 包括执行、等待、修复、让位后排队及完成条件成立后等待插入行为结束；SUCCEEDED、FAILED、CANCELLED 为执行终态。
 * 是否为运行任务由人物的 active_task_id 表示。
 */
typedef enum {
    DOMINO_TASK_EXEC_STATUS_NONE = 0,
    DOMINO_TASK_EXEC_STATUS_OPEN,
    DOMINO_TASK_EXEC_STATUS_SUCCEEDED,
    DOMINO_TASK_EXEC_STATUS_FAILED,
    DOMINO_TASK_EXEC_STATUS_CANCELLED,
    DOMINO_TASK_EXEC_STATUS_COUNT,
} DOMINO_TASK_EXEC_STATUS;

/** @brief 行为执行记录状态；记录只在启动时建立，因此没有 PENDING 或 SKIPPED。 */
typedef enum {
    DOMINO_BEHAVIOR_STATUS_NONE = 0,
    DOMINO_BEHAVIOR_STATUS_ACTIVE,
    DOMINO_BEHAVIOR_STATUS_WAITING,  ///< 已启动，只累计具体规则允许的进度和效果。
    DOMINO_BEHAVIOR_STATUS_SUCCEEDED,
    DOMINO_BEHAVIOR_STATUS_FAILED,
    DOMINO_BEHAVIOR_STATUS_CANCELLED,
    DOMINO_BEHAVIOR_STATUS_COUNT,
} DOMINO_BEHAVIOR_STATUS;

/** @brief 任务或行为结束、撤销的业务原因。 */
typedef enum {
    DOMINO_END_REASON_NONE = 0,
    DOMINO_END_REASON_COMPLETED,          ///< 完成条件成立。
    DOMINO_END_REASON_NEED_SATISFIED,     ///< 需求已被其他途径满足，本次工作不再需要。
    DOMINO_END_REASON_CONDITION_LOST,     ///< 执行条件失效，例如睡眠被禁止。
    DOMINO_END_REASON_DEPENDENCY_BROKEN,  ///< 所需的行为依赖已不可能满足。
    DOMINO_END_REASON_DEADLINE_MISSED,    ///< 截止点前未完成。
    DOMINO_END_REASON_TASK_ENDED,         ///< 所属任务已结束，当前行为随之停止。
    DOMINO_END_REASON_PLANNER_CANCELLED,  ///< 任务规划器建议的合法取消。
    DOMINO_END_REASON_YIELDED,            ///< 所属任务完全让位，运行中的行为随之取消。
    DOMINO_END_REASON_MOTIVATION_ENDED,   ///< 所属动机已终结，尚未启动的任务随之撤销。
    DOMINO_END_REASON_COUNT,
} DOMINO_END_REASON;

/** @brief 工作身份中的业务来源类别；来源是负责这件事的业务记录，不是提出建议的模块。 */
typedef enum {
    DOMINO_TASK_SOURCE_NONE = 0,
    DOMINO_TASK_SOURCE_MOTIVATION,  ///< 没有独立业务记录的工作，使用动机身份及需求轮次。
    DOMINO_TASK_SOURCE_STIMULUS,    ///< 回应外部信息：来源为外部信息来源，轮次为人物的外部信息轮次。
    DOMINO_TASK_SOURCE_COUNT,
} DOMINO_TASK_SOURCE;

/** @brief 人物待处理外部信息的类别；由外部信息输入登记（当前没有登记入口），由运行任务的应激规则选择完全让位或接受插入。 */
typedef enum {
    DOMINO_HUMAN_STIMULUS_NONE = 0,
    DOMINO_HUMAN_STIMULUS_CONTACT,  ///< 联系：来电、搭讪等需要回应的外部信息。
    DOMINO_HUMAN_STIMULUS_COUNT,
} DOMINO_HUMAN_STIMULUS_KIND;

/** @brief 同一任务内行为之间的依赖类别；依赖只指向本任务计划中更早的步骤或正在运行的行为，因此天然无环。 */
typedef enum {
    DOMINO_BEHAVIOR_DEPENDENCY_NONE = 0,
    DOMINO_BEHAVIOR_DEPENDENCY_START,     ///< 启动依赖：来源行为成功后本步骤才可启动；来源失败或取消则本步骤失效。
    DOMINO_BEHAVIOR_DEPENDENCY_WHILE,     ///< 伴随依赖：本步骤只在来源行为运行期间启动和运行，来源结束即随之取消。
    DOMINO_BEHAVIOR_DEPENDENCY_COMPLETE,  ///< 完成依赖：本行为成功结束时要求来源行为已经成功；来源失败或取消则本行为失败。
    DOMINO_BEHAVIOR_DEPENDENCY_KIND_COUNT,
} DOMINO_BEHAVIOR_DEPENDENCY_KIND;

/** @brief 来源结束时归并到依赖方的结果；PENDING 表示来源尚未结束。 */
typedef enum {
    DOMINO_BEHAVIOR_DEPENDENCY_PENDING = 0,
    DOMINO_BEHAVIOR_DEPENDENCY_SATISFIED,  ///< 启动／完成依赖的来源已成功。
    DOMINO_BEHAVIOR_DEPENDENCY_BROKEN,     ///< 来源已失败或取消；伴随依赖的来源已结束。
    DOMINO_BEHAVIOR_DEPENDENCY_STATE_COUNT,
} DOMINO_BEHAVIOR_DEPENDENCY_STATE;

/** @brief 行为步骤与执行记录共用的持久标志。 */
enum {
    DOMINO_BEHAVIOR_FLAG_INSERTED = 1U << 0U,  ///< 回应外部信息插入的辅助行为：与本任务其他行为无依赖，不计入任务成功证明和进度。
};

/** @brief 一条行为依赖；来源用本任务内的步骤序号表示，来源结束时把结果归并到 state。 */
typedef struct DominoBehaviorDependency {
    uint32_t step_seq;  ///< 来源步骤序号；kind 为 NONE 时为 0。候选中可为带局部引用高位的条目内序号。
    uint8_t kind;       ///< DOMINO_BEHAVIOR_DEPENDENCY_KIND。
    uint8_t state;      ///< DOMINO_BEHAVIOR_DEPENDENCY_STATE。
    uint8_t reserved[2];
} DominoBehaviorDependency;

/** @brief 任务持久标志。 */
enum {
    DOMINO_TASK_FLAG_DEADLINE_VALID = 1U << 0U,             ///< deadline_ms 有效。
    DOMINO_TASK_FLAG_DEADLINE_INCLUSIVE = 1U << 1U,         ///< 恰在截止时刻完成仍算按时。
    DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID = 1U << 2U,       ///< behavior_retry_at_ms 有效：该时刻前不重投行为规划。
    DOMINO_TASK_FLAG_REVOKED = 1U << 3U,                    ///< 未启动工作已被依法撤销，等待回收；不制造执行终态。
    DOMINO_TASK_FLAG_NEED_SATISFIED_EXTERNALLY = 1U << 4U,  ///< 外部输入在尚无成功行为时满足了完成指标。
    DOMINO_TASK_FLAG_PREEMPT = 1U << 5U,                    ///< 回应外部信息的任务：首个行为可启动时请求运行任务完全让位。
    DOMINO_TASK_FLAG_COMPLETION_REACHED = 1U << 6U,         ///< 完成条件已成立，等待插入的辅助行为结束后提交成功。
};

/** @brief 每个动机的一条成功实施明细；时刻为任务实际开始与成功结束的业务时刻。 */
typedef struct DominoMotivationHistory {
    domino_task_id_t task_id;
    domino_runtime_ms_t started_at_ms;
    domino_runtime_ms_t ended_at_ms;
} DominoMotivationHistory;

/** @brief 正式动机记录；由动机规划器建议、执行器接受后建立，可以先存在而没有任务。
 * @note 动机只说明人物长期要满足的需求，不表达当前是否需要行动；是否为它安排工作、何时再看由规划器判断。
 * 实施次数每个不同成功任务计一次，累计保存；最近明细为环形，满 10 条后覆盖最早一条。
 * need_round 是睡眠工作身份的需求轮次：首次接受睡眠任务计划时建立第一轮，重试、替换、回收和读档都不自动增加。
 */
typedef struct DominoMotivation {
    DominoMotivationHistory history_list[DOMINO_MOTIVATION_HISTORY_COUNT_MAX];  ///< 环形历史，history_head_index 指向最早一条。
    uint32_t success_count;                                                     ///< 累计成功实施次数；已实施由其大于 0 推导。
    domino_motivation_id_t motivation_id;                                       ///< 非零正式 ID。
    domino_human_id_t owner_human_id;
    uint32_t need_round;             ///< 最近已接受的需求轮次，0 表示尚未接受任何轮次。
    domino_task_id_t round_task_id;  ///< 承接当前未结束轮次的任务，0 表示本轮已结束。
    uint8_t type;                    ///< DOMINO_MOTIVATION_TYPE。
    uint8_t status;                  ///< DOMINO_MOTIVATION_STATUS。
    uint8_t history_count;           ///< 有效明细数，0..10。
    uint8_t history_head_index;      ///< 最早一条明细的位置。
    uint8_t last_failure_reason;     ///< 最近一次失败或取消归并的原因，DOMINO_END_REASON。
} DominoMotivation;

/** @brief 已接受的行为计划项，只保存通道、类型、参数、依赖和约束值，没有正式行为 ID 或执行状态。
 * @note 同一通道的步骤按计划中的先后依次执行；step_seq 在接受时分配，本任务内唯一，启动时复制到行为记录。
 */
typedef struct DominoBehaviorPlanItem {
    DominoBehaviorDependency dependency_list[DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX];  ///< kind 为 NONE 的项为空。
    domino_runtime_ms_t target_effective_ms;                                         ///< 正的目标有效时长或工作量。
    uint32_t param;                                                                  ///< 类型参数；通用计时行为表示精力消耗周期（毫秒，0 为不消耗）。
    uint32_t step_seq;                                                               ///< 本任务内的步骤序号；候选中不使用，接受时分配。
    uint8_t type;                                                                    ///< DOMINO_BEHAVIOR_TYPE。
    uint8_t channel_index;  ///< DOMINO_BEHAVIOR_CHANNEL_*：主通道为主行为，辅助通道为辅助行为。
    uint8_t flags;          ///< DOMINO_BEHAVIOR_FLAG_*。
    uint8_t reserved;
} DominoBehaviorPlanItem;

/** @brief 统一任务记录：接受计划时建立，拥有稳定 task_id；首次启动在同一记录上附加执行部分。
 * @note plan_item_list 是本任务独占的未来行为计划连续块（plan_item_count 项），替换、消费或回收时由 human store 释放；
 * 结构体按值复制时只复制指针，调用方不得据此建立第二份所有权。
 * plan_version 只用于本会话的异步依赖核对，在本次 Init 内全局唯一、不复用，不持久化。
 * 执行部分字段（started_at_ms 起）仅在 exec_status 非 NONE 时有效；累计事实在主行为终态提交时同次归并。
 */
typedef struct DominoTask {
    DominoBehaviorPlanItem* plan_item_list;  ///< 独占未来步骤，plan_item_count 为 0 时为空指针。
    uint64_t plan_version;                   ///< 运行态：非零计划版本。
    domino_task_id_t task_id;
    domino_human_id_t owner_human_id;
    domino_motivation_id_t motivation_id;  ///< 所属动机；一个任务只归属一个动机。
    uint32_t work_source_id;               ///< 工作身份：业务来源记录 ID。
    uint32_t work_round;                   ///< 工作身份：工作轮次。
    uint32_t last_step_seq;                ///< 最后已分配的步骤序号，0 表示尚未分配。
    domino_runtime_ms_t accepted_at_ms;    ///< 计划首次被接受的业务时刻。
    domino_runtime_ms_t deadline_ms;       ///< 完成截止点，DOMINO_TASK_FLAG_DEADLINE_VALID 时有效。
    domino_runtime_ms_t behavior_retry_at_ms;
    domino_runtime_ms_t started_at_ms;                                             ///< 执行部分：实际首次开始时刻，0 合法。
    domino_runtime_ms_t ended_at_ms;                                               ///< 执行部分：终态时刻。
    domino_runtime_ms_t progress_ms;                                               ///< 执行部分：已终结主行为归并的有效进度合计。
    domino_behavior_id_t current_behavior_id_list[DOMINO_BEHAVIOR_CHANNEL_COUNT];  ///< 各通道唯一未终结行为记录，0 表示没有。
    domino_behavior_id_t last_behavior_id;                                         ///< 仍保留的最近一条终态行为记录，0 表示没有。
    uint16_t main_succeeded_count;                                                 ///< 成功证明：本任务已成功结束的主行为数。
    uint16_t main_failed_count;
    uint16_t main_cancelled_count;
    uint8_t type;                   ///< DOMINO_TASK_TYPE。
    uint8_t stimulus_kind;          ///< 回应外部信息的任务所回应的类别；其他任务为 NONE。
    uint8_t exec_status;            ///< DOMINO_TASK_EXEC_STATUS。
    uint8_t plan_item_count;        ///< 未来行为计划项数。
    uint8_t behavior_record_count;  ///< 运行态：仍在池中的本任务行为记录数，加载后由行为记录重建。
    uint8_t work_source_kind;       ///< 工作身份：DOMINO_TASK_SOURCE。
    uint8_t target_value;           ///< 类型完成指标；睡眠为目标精力。
    uint8_t end_reason;             ///< 任务终态或撤销原因，DOMINO_END_REASON。
    uint8_t last_behavior_end_reason;
    uint8_t flags;  ///< DOMINO_TASK_FLAG_*。
} DominoTask;

/** @brief 行为执行记录；首次启动时分配正式 ID 建立，同一任务每个通道至多一条未终结记录。
 * @note 共同结算游标只保存在人物上；记录保留自身有效进度，周期效果相位由有效进度对周期取余得到，不另存。
 * 依赖从计划项复制，来源结束时由任务层同次归并结果。
 */
typedef struct DominoBehavior {
    DominoBehaviorDependency dependency_list[DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX];
    domino_behavior_id_t behavior_id;
    domino_task_id_t task_id;  ///< 唯一所属任务。
    uint32_t step_seq;         ///< 来源计划项的步骤序号。
    domino_runtime_ms_t started_at_ms;
    domino_runtime_ms_t ended_at_ms;            ///< 仅终态有效。
    domino_runtime_ms_t target_effective_ms;    ///< 启动时从计划项复制的目标有效时长。
    domino_runtime_ms_t effective_progress_ms;  ///< 已累计的有效进度，启动时为 0，不含冻结和无收益等待；周期效果在其整周期边界发放。
    uint32_t param;                             ///< 启动时从计划项复制的类型参数；通用计时行为表示精力消耗周期（毫秒，0 为不消耗）。
    uint8_t type;                               ///< DOMINO_BEHAVIOR_TYPE。
    uint8_t status;                             ///< DOMINO_BEHAVIOR_STATUS。
    uint8_t end_reason;                         ///< DOMINO_END_REASON。
    uint8_t channel_index;                      ///< 运行通道：主通道为主行为。
    uint8_t flags;                              ///< DOMINO_BEHAVIOR_FLAG_*。
    uint8_t reserved[3];
} DominoBehavior;

/** @brief 任务执行部分是否已进入终态。 */
static inline bool dominoTaskIsTerminal(const DominoTask* task_ptr) {
    return task_ptr->exec_status >= DOMINO_TASK_EXEC_STATUS_SUCCEEDED;
}

/** @brief 任务是否仍承担未完成责任：未撤销且没有执行终态。 */
static inline bool dominoTaskIsLive(const DominoTask* task_ptr) {
    return !dominoTaskIsTerminal(task_ptr) && (task_ptr->flags & DOMINO_TASK_FLAG_REVOKED) == 0U;
}

/** @brief 行为记录是否已终结。 */
static inline bool dominoBehaviorIsTerminal(const DominoBehavior* behavior_ptr) {
    return behavior_ptr->status >= DOMINO_BEHAVIOR_STATUS_SUCCEEDED;
}

#endif
