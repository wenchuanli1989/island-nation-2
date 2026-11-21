#ifndef DOMINO_ENGINE_HUMAN_TASK_ENTRY_H
#define DOMINO_ENGINE_HUMAN_TASK_ENTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "../behavior/entry.h"
#include "domino_shared_human_task.h"

/** @file entry.h
 * @brief 任务规则接口：每种任务类型实现只读决策、计划缺口、应激选择和外部输入处理。
 * @note 所有边界检查（定时推进、条件变化、依赖变化、候选提交、Stop）都调用同一个只读决策；决策只返回选择，
 * 由推进层统一应用，规则本身不写世界、不启动任务。任务的成功证明只取主通道行为。
 */

/** @brief 任务决策的只读输入；行为可以是局部副本，预测与结算复用同一决策。 */
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

/** @brief 决策结果。 */
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

/** @brief 每种任务类型的规则；函数指针都不能为空。 */
typedef struct DominoTaskRules {
    /** @brief 只读决策；成功须同时满足类型的完成条件和主行为成功证明。 */
    DominoTaskDecision (*decide)(const DominoTaskDecisionInput* input_ptr);
    /** @brief 自身剩余工作是否缺少安排（需要行为规划）；不计插入的辅助步骤。 */
    bool (*has_plan_gap)(const DominoTaskDecisionInput* input_ptr);
    /** @brief 当前条件下是否值得发起行为规划；条件明确受阻时返回 false，等条件变化。 */
    bool (*can_plan_behavior)(const DominoTaskDecisionInput* input_ptr);
    /** @brief 应激：作为运行任务遇到外部信息时选择完全让位或接受插入，返回 DOMINO_TASK_REACTION。 */
    uint8_t (*react)(const DominoTaskDecisionInput* input_ptr, uint8_t stimulus_kind);
    /** @brief 外部精力输入写入后、决策之前调用，记录本类型需要的输入事实。 */
    void (*on_energy_input)(DominoTask* task_ptr, uint8_t energy);
} DominoTaskRules;

/** @brief 任务类型的规则，未实现返回空指针。 */
const DominoTaskRules* dominoHumanGetTaskRules(uint8_t type);

/** @brief now_ms 是否仍在截止点之内（截止时刻本身是否算按时由任务标志决定）。 */
bool dominoHumanTaskIsWithinDeadline(const DominoTask* task_ptr, domino_runtime_ms_t now_ms);

/** @brief 任务自身（非插入）在 channel_mask 通道上是否还有当前行为或未来步骤。 */
bool dominoHumanTaskHasOwnWork(const DominoTaskDecisionInput* input_ptr, uint8_t channel_mask);

/** @brief 是否还有插入的辅助行为在运行，或插入的辅助步骤尚未启动。 */
bool dominoHumanTaskHasInsertedWork(const DominoTaskDecisionInput* input_ptr);

/** @brief 未来步骤中是否有依赖已失效、永远不能启动的步骤。 */
bool dominoHumanTaskHasBrokenStep(const DominoTask* task_ptr);

/** @brief 全部通道的位集合。 */
#define DOMINO_BEHAVIOR_CHANNEL_MASK_ALL ((uint8_t)((1U << DOMINO_BEHAVIOR_CHANNEL_COUNT) - 1U))

#endif
