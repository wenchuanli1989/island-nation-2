#ifndef DOMINO_ENGINE_HUMAN_NEEDS_H
#define DOMINO_ENGINE_HUMAN_NEEDS_H

#include <stdint.h>

#include "domino_shared_human.h"

/** @file needs.h
 * @brief 自然需求的基础计算，真实结算和只读预测共用（宪法 9.3）。
 */

/** @brief 六项需求的局部值。 */
typedef struct DominoHumanNeedValues {
    uint8_t energy;
    uint8_t food;
    uint8_t water;
    uint8_t hygiene;
    uint8_t recreation;
    uint8_t social;
} DominoHumanNeedValues;

/** @brief 从人物 Data 复制需求值。 */
DominoHumanNeedValues dominoHumanReadNeeds(const DominoHumanData* data_ptr);

/** @brief 把需求值写回人物 Data；调用方须持写权限。 */
void dominoHumanWriteNeeds(DominoHumanData* data_ptr, const DominoHumanNeedValues* values_ptr);

/** @brief 计算区间 [begin_ms, end_ms] 内的自然衰减：每跨过一个业务时间整分钟边界衰减一次，降到 0 为止。
 * @pre end_ms 不早于 begin_ms。
 * @note 整分钟相位就是时刻对一分钟取余，不另存。
 */
DominoHumanNeedValues dominoHumanCalcNaturalNeeds(const DominoHumanNeedValues* values_ptr, domino_runtime_ms_t begin_ms, domino_runtime_ms_t end_ms);

/** @brief 从 from_ms 到下一个整分钟边界的毫秒数，1..60000。 */
static inline domino_runtime_ms_t dominoHumanMsUntilNeedTick(domino_runtime_ms_t from_ms) {
    return 60000U - (from_ms % 60000U);
}

/** @brief 只计自然衰减时，精力从 from_ms 的 energy 首次降到不高于 threshold 所需的毫秒数。
 * @return 已不高于阈值返回 0；用 64 位返回精确值，由调用方判断是否超出业务时间范围。
 */
uint64_t dominoHumanNaturalMsUntilEnergyAtMost(uint8_t energy, domino_runtime_ms_t from_ms, uint8_t threshold);

#endif
