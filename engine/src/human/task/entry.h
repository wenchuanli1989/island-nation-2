#ifndef DOMINO_ENGINE_HUMAN_TASK_ENTRY_H
#define DOMINO_ENGINE_HUMAN_TASK_ENTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "../behavior/entry.h"
#include "domino_shared_human_task.h"

/** @file entry.h
 * @brief 任务统一接口：只读决策、计划缺口、应激选择、外部输入处理及计划查询辅助；具体任务类型实现本类型规则。
 * @note 定时推进、外部条件变化、行为依赖变化、候选提交后的检查和 Stop 都调用同一个只读决策入口；
 * 决策只返回选择，由推进层协调并通过统一应用入口写入，任务规则不自行启动后继、递归推进或收发消息。
 * 同一时间只有一个运行任务；运行任务的三个通道各至多一个当前行为，任务成功证明只取主通道行为。
 */

/** @brief 任务决策的只读输入；行为可以是局部副本，便于预测复用同一决策。 */
typedef struct DominoTaskDecisionInput {
    const DominoTask* task_ptr;
    const DominoBehavior* behavior_list[DOMINO_BEHAVIOR_CHANNEL_COUNT];  ///< 各通道当前未终结行为，没有时为空指针。
    domino_runtime_ms_t now_ms;                                          ///< 本次边界时刻。
    uint8_t energy;                                                      ///< 共同结算后的精力。
    DominoHumanConditionView condition;
} DominoTaskDecisionInput;

/** @brief 任务决策选择。 */
typedef enum {
    DOMINO_TASK_DECISION_CONTINUE = 0,  ///< 继续已有执行或等待已有依据，不改变状态。
    DOMINO_TASK_DECISION_REPAIR,        ///< 终结指定通道的当前行为并保留任务 OPEN，记录缺口供修复。
    DOMINO_TASK_DECISION_SUCCEED,       ///< 完成条件成立；仍有插入的辅助行为时等其结束再提交成功。
    DOMINO_TASK_DECISION_FAIL,
    DOMINO_TASK_DECISION_CANCEL,
    DOMINO_TASK_DECISION_REVOKE,  ///< 依法撤销尚未启动的工作，不制造执行终态。
    DOMINO_TASK_DECISION_COUNT,
} DOMINO_TASK_DECISION_KIND;

/** @brief 决策结果：选择、REPAIR 终结的通道及终态、原因。 */
typedef struct DominoTaskDecision {
    uint8_t kind;                 ///< DOMINO_TASK_DECISION_KIND。
    uint8_t behavior_end_status;  ///< REPAIR 时当前行为的终态：CANCELLED 或 FAILED。
    uint8_t channel_mask;         ///< REPAIR 时需要终结的通道位集合（1 << 通道）。
    uint8_t reason;               ///< DOMINO_END_REASON。
} DominoTaskDecision;

/** @brief 运行任务遇到待处理外部信息时的应激选择。 */
typedef enum {
    DOMINO_TASK_REACTION_YIELD = 0,  ///< 完全让位：回应任务首个行为可启动时，终结全部当前行为、清空未来步骤并回到待执行队列。
    DOMINO_TASK_REACTION_INSERT,     ///< 接受插入：继续运行，为外部信息插入与自身无依赖的辅助行为。
    DOMINO_TASK_REACTION_COUNT,
} DOMINO_TASK_REACTION;

/** @brief 每种任务类型的规则；全部函数指针必须非空。 */
typedef struct DominoTaskRules {
    /** @brief 统一只读决策；成功必须同时满足类型完成条件与本任务已归并的主行为成功证明。 */
    DominoTaskDecision (*decide)(const DominoTaskDecisionInput* input_ptr);
    /** @brief 自身剩余工作是否缺少可行安排（需要行为规划）；只读，不考虑插入的辅助步骤。 */
    bool (*has_plan_gap)(const DominoTaskDecisionInput* input_ptr);
    /** @brief 当前条件下是否值得发起行为规划；条件明确受阻时等待事件。 */
    bool (*can_plan_behavior)(const DominoTaskDecisionInput* input_ptr);
    /** @brief 应激：作为运行任务遇到待处理外部信息时选择完全让位或接受插入；只读，返回 DOMINO_TASK_REACTION。 */
    uint8_t (*react)(const DominoTaskDecisionInput* input_ptr, uint8_t stimulus_kind);
    /** @brief 外部精力输入写入后调用，可记录本类型需要的输入事实；在决策之前执行。 */
    void (*on_energy_input)(DominoTask* task_ptr, uint8_t energy);
} DominoTaskRules;

/** @brief 查询任务类型规则；未实现返回空指针。 */
const DominoTaskRules* dominoHumanGetTaskRules(uint8_t type);

/** @brief 截止点检查：返回 now 是否仍在允许完成的范围内（包含或不包含截止时刻按任务标志）。 */
bool dominoHumanTaskWithinDeadline(const DominoTask* task_ptr, domino_runtime_ms_t now_ms);

/** @brief 任务自身（非插入）在 channel_mask 所列通道上是否仍有当前行为或未来步骤。 */
bool dominoHumanTaskHasOwnWork(const DominoTaskDecisionInput* input_ptr, uint8_t channel_mask);

/** @brief 是否仍有插入的辅助行为在运行或插入的辅助步骤尚未启动。 */
bool dominoHumanTaskHasInsertedWork(const DominoTaskDecisionInput* input_ptr);

/** @brief 未来步骤中是否有依赖已确定不可满足、永远不能启动的步骤。 */
bool dominoHumanTaskHasBrokenStep(const DominoTask* task_ptr);

/** @brief 全部通道的位集合。 */
#define DOMINO_BEHAVIOR_CHANNEL_MASK_ALL ((uint8_t)((1U << DOMINO_BEHAVIOR_CHANNEL_COUNT) - 1U))

#endif
