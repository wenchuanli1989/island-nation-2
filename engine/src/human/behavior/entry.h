#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_human_task.h"

/** @file entry.h
 * @brief 行为统一接口：校验计划步骤、只读基础计算、结果判定与执行条件；具体类型实现本类型规则。
 * @note 基础计算只返回局部值，由真实结算和只读预测共同调用；不写世界、不提交终态、不读取时钟、不前移人物游标。
 */

/** @brief 行为基础计算的输入；区间由推进层统一给定。 */
typedef struct DominoBehaviorCalcInput {
    domino_runtime_ms_t progress_ms;  ///< 当前有效进度。
    domino_runtime_ms_t target_ms;    ///< 目标有效时长。
    domino_runtime_ms_t span_ms;      ///< 共同区间长度。
    uint32_t param;
    uint8_t status;     ///< DOMINO_BEHAVIOR_STATUS；只有 ACTIVE 累计正常进度。
    bool condition_ok;  ///< 本区间执行条件保持成立。
} DominoBehaviorCalcInput;

/** @brief 行为基础计算的局部结果。 */
typedef struct DominoBehaviorCalcOutput {
    domino_runtime_ms_t progress_ms;
    int32_t energy_delta;                  ///< 本区间对精力的有符号效果，由调用方按 0..100 截断后应用。
    domino_runtime_ms_t next_boundary_ms;  ///< 区间结束后距下一效果或完成边界的毫秒数，仅 has_boundary 时有效。
    bool has_boundary;
} DominoBehaviorCalcOutput;

/** @brief 人物条件的只读视图。 */
typedef struct DominoHumanConditionView {
    bool sleep_forbidden_flag;
} DominoHumanConditionView;

/** @brief 每种行为类型的规则。 */
typedef struct DominoBehaviorRules {
    /** @brief 直接检查计划步骤的类型参数，不要求先有执行记录。 */
    bool (*validate_step)(const DominoBehaviorPlanItem* item_ptr);
    /** @brief 当前人物条件下能否执行（硬条件）。 */
    bool (*can_run)(const DominoHumanConditionView* view_ptr);
    /** @brief 只读基础计算。 */
    void (*calc)(const DominoBehaviorCalcInput* input_ptr, DominoBehaviorCalcOutput* output_ptr);
    /** @brief 共同结算后按本类型条件判定：返回 ACTIVE 表示继续，或 SUCCEEDED／FAILED。 */
    uint8_t (*judge)(const DominoBehavior* behavior_ptr);
} DominoBehaviorRules;

/** @brief 查询行为类型规则；未实现返回空指针。 */
const DominoBehaviorRules* dominoHumanGetBehaviorRules(uint8_t type);

/** @brief 对行为记录（可为局部副本）调用基础计算。 */
DominoBehaviorCalcOutput dominoHumanCalcBehavior(const DominoBehavior* behavior_ptr, const DominoHumanConditionView* view_ptr,
                                                 domino_runtime_ms_t span_ms);

/** @brief 有效进度从 from_ms 增至 to_ms 时跨过的整周期边界数；周期效果相位即有效进度对周期取余，不另存。
 * @note 调用方保证 period_ms 非零且 to_ms 不早于 from_ms。
 */
static inline uint32_t dominoBehaviorCountPeriods(domino_runtime_ms_t from_ms, domino_runtime_ms_t to_ms, uint32_t period_ms) {
    return (to_ms / period_ms) - (from_ms / period_ms);
}

/** @brief 从有效进度 progress_ms 起距离下一个整周期边界的有效毫秒数，1..period_ms；period_ms 须非零。 */
static inline domino_runtime_ms_t dominoBehaviorCalcPeriodGapMs(domino_runtime_ms_t progress_ms, uint32_t period_ms) {
    return period_ms - (progress_ms % period_ms);
}

/** @brief 把有符号效果截断到 0..100 后返回新精力。 */
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
