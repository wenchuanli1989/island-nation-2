#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_human_task.h"

/** @file entry.h
 * @brief 行为规则接口：每种行为类型实现步骤校验、执行条件、基础计算和结果判定。
 * @note 基础计算只返回局部值，真实结算与只读预测共用；不写世界、不读时钟。
 */

/** @brief 行为基础计算的输入。 */
typedef struct DominoBehaviorCalcInput {
    domino_runtime_ms_t progress_ms;  ///< 当前有效进度。
    domino_runtime_ms_t target_ms;    ///< 目标有效时长。
    domino_runtime_ms_t span_ms;      ///< 本段区间长度。
    uint32_t param;
    uint8_t status;     ///< DOMINO_BEHAVIOR_STATUS；只有 ACTIVE 累计正常进度。
    bool condition_ok;  ///< 本段执行条件成立。
} DominoBehaviorCalcInput;

/** @brief 行为基础计算的结果。 */
typedef struct DominoBehaviorCalcOutput {
    domino_runtime_ms_t progress_ms;
    int32_t energy_delta;                  ///< 本段对精力的影响，由调用方截断到 0..100 后应用。
    domino_runtime_ms_t next_boundary_ms;  ///< 本段结束后到下一个效果或完成边界的毫秒数，has_boundary 时有效。
    bool has_boundary;
} DominoBehaviorCalcOutput;

/** @brief 行为执行条件需要的人物状态。 */
typedef struct DominoHumanConditionView {
    bool sleep_forbidden_flag;
} DominoHumanConditionView;

/** @brief 每种行为类型的规则。 */
typedef struct DominoBehaviorRules {
    /** @brief 检查计划步骤的类型参数。 */
    bool (*validate_step)(const DominoBehaviorPlanItem* item_ptr);
    /** @brief 当前人物状态下能否执行。 */
    bool (*can_run)(const DominoHumanConditionView* view_ptr);
    /** @brief 只读基础计算。 */
    void (*calc)(const DominoBehaviorCalcInput* input_ptr, DominoBehaviorCalcOutput* output_ptr);
    /** @brief 结算后判定：ACTIVE 继续，或 SUCCEEDED／FAILED。 */
    uint8_t (*judge)(const DominoBehavior* behavior_ptr);
} DominoBehaviorRules;

/** @brief 行为类型的规则，未实现返回空指针。 */
const DominoBehaviorRules* dominoHumanGetBehaviorRules(uint8_t type);

/** @brief 对行为记录（可为局部副本）做基础计算。 */
DominoBehaviorCalcOutput dominoHumanCalcBehavior(const DominoBehavior* behavior_ptr, const DominoHumanConditionView* view_ptr,
                                                 domino_runtime_ms_t span_ms);

/** @brief 有效进度从 from_ms 增至 to_ms 跨过的整周期边界数；周期相位即有效进度对周期取余，不另存。
 * @pre period_ms 非零，to_ms 不早于 from_ms。
 */
static inline uint32_t dominoBehaviorCountPeriods(domino_runtime_ms_t from_ms, domino_runtime_ms_t to_ms, uint32_t period_ms) {
    return (to_ms / period_ms) - (from_ms / period_ms);
}

/** @brief 从有效进度 progress_ms 到下一个整周期边界的毫秒数，1..period_ms；period_ms 须非零。 */
static inline domino_runtime_ms_t dominoBehaviorMsUntilPeriod(domino_runtime_ms_t progress_ms, uint32_t period_ms) {
    return period_ms - (progress_ms % period_ms);
}

/** @brief 应用精力变化并截断到 0..100。 */
static inline uint8_t dominoHumanApplyEnergyDelta(uint8_t energy, int32_t delta) {
    int32_t value = (int32_t)energy + delta;
    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }
    return (uint8_t)value;
}

#endif
