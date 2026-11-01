#ifndef DOMINO_ENGINE_HUMAN_NEEDS_H
#define DOMINO_ENGINE_HUMAN_NEEDS_H

#include <stdint.h>

#include "domino_shared_human.h"

/** @file needs.h
 * @brief 执行器内部的自然需求结算与睡眠阈值预测。
 */

/** @brief 内部只读预测仅按自然衰减首次达到睡眠触发精力的关注时刻，保留完整分钟相位。
 * @param data_ptr 已共同结算需求及活动任务到当前时刻，且精力高于睡眠触发阈值的人物 data。
 * @return 严格晚于当前时刻的绝对关注时刻；超出 32 位毫秒范围时致命退出。
 * @note 不结算或修改人物，不检查授权、地点及待办任务。0 与 UINT32_MAX 均为合法时刻，不充当无事件标志。
 * 已达阈值时由执行器直接检查规划条件。
 */
domino_runtime_ms_t dominoHumanGetSleepTriggerMs(const DominoHumanData* data_ptr);

/** @brief 内部整数衰减 helper；先比较可扣分钟数，避免乘法溢出及无符号减法下溢。 */
static inline uint8_t dominoHumanDecayNeed(uint8_t value, uint32_t minute_count, uint8_t decay_per_minute) {
    if (decay_per_minute == 0U) {
        return value;
    }
    if (minute_count > (uint32_t)value / decay_per_minute) {
        return 0U;
    }
    return (uint8_t)(value - minute_count * decay_per_minute);
}

/** @brief 内部入口：对已校验的人物补结算完整现实分钟，保留不足一分钟的时间。
 * @param data_ptr 六项需求在合法范围内、由可信加载及受控写入维护，且本次已确认结算时刻有效的人物 data。
 * @param now_ms 不早于自然结算检查点的累计未冻结现实毫秒。
 * @note 供执行器按各通道合并后的效果边界调用；调用者须独占写入，并在任务效果之前结算自然变化。
 * 不能绕过共同结算而先处理整段自然变化，否则会改变自然衰减与任务收益的时间顺序。
 */
void dominoHumanApplyNaturalNeeds(DominoHumanData* data_ptr, domino_runtime_ms_t now_ms);

#endif
