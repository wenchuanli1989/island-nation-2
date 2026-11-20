#ifndef DOMINO_ENGINE_HUMAN_NEEDS_H
#define DOMINO_ENGINE_HUMAN_NEEDS_H

#include <stdint.h>

#include "domino_shared_human.h"

/** @file needs.h
 * @brief 自然需求的只读基础计算，由真实结算和只读预测共同调用；周期跨越、余量和取整只实现一份。
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

/** @brief 从人物 Data 复制需求局部值。 */
DominoHumanNeedValues dominoHumanReadNeeds(const DominoHumanData* data_ptr);

/** @brief 把需求局部值写回人物 Data；调用方须持有写入权限。 */
void dominoHumanWriteNeeds(DominoHumanData* data_ptr, const DominoHumanNeedValues* values_ptr);

/** @brief 只读计算区间 [begin_ms, end_ms] 内的自然变化：每跨过一个业务时间整分钟边界按整数衰减一次。
 * @param begin_ms 区间起点；调用方保证 end_ms 不早于 begin_ms。
 * @note 整分钟边界只由未冻结的累计业务时间决定，相位即时刻对一分钟取余，不另存；不读取时钟、不修改世界，减法均不回绕。
 */
DominoHumanNeedValues dominoHumanCalcNaturalNeeds(const DominoHumanNeedValues* values_ptr, domino_runtime_ms_t begin_ms, domino_runtime_ms_t end_ms);

/** @brief 从 from_ms 起距离下一个业务时间整分钟边界的毫秒数，1..60000。 */
static inline domino_runtime_ms_t dominoHumanNextNeedTickMs(domino_runtime_ms_t from_ms) {
    return 60000U - (from_ms % 60000U);
}

/** @brief 仅按自然衰减，从 from_ms 时的 energy 出发精力首次不高于 threshold 所需的毫秒数。
 * @return 已不高于阈值返回 0；以 64 位返回精确时长，不截断或饱和，由调用方按业务时间范围判断能否登记。
 */
uint64_t dominoHumanNaturalMsUntilEnergyAtMost(uint8_t energy, domino_runtime_ms_t from_ms, uint8_t threshold);

#endif
