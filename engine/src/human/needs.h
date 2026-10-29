#ifndef DOMINO_ENGINE_HUMAN_NEEDS_H
#define DOMINO_ENGINE_HUMAN_NEEDS_H

#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"

/** @file needs.h
 * @brief 人物需求校验与结算；失败仅返回结果，由调用方结合业务上下文决定诊断和错误处理。
 */

/** @brief 校验六项需求范围及自然结算时刻，不修改人物。
 * @param data_ptr 必需的人物 data。
 * @param now_ms 本次结算的累计未冻结现实毫秒。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；需求超出 0..100 返回 ERR_INVALID_DATA；
 * now_ms 早于已结算的自然检查点返回 ERR_INVALID_PARAM。
 */
DOMINO_CODE dominoHumanValidateNeedSettlement(const DominoHumanData* data_ptr, domino_runtime_ms_t now_ms);

/** @brief 内部只读预测空闲人物首次达到睡眠触发精力的关注时刻，保留自然结算的完整分钟相位。
 * @param data_ptr 已在本次调用链中完成到 now_ms 的空闲需求结算、确认三槽为空，且需求与活动槽未再改变的人物 data。
 * @param now_ms 本次判断的累计未冻结现实毫秒，不早于 needs_updated_at_ms；不读取时钟，调用方须保证人物业务时间不倒退。
 * @param due_out_ptr 必需输出，成功时写入绝对关注时刻；当前已达阈值时写入 now_ms，错误时保持原值。
 * @return CODE_OK 成功；未来阈值超出 32 位毫秒范围返回 ERR_OUT_OF_RANGE。
 * @note 不结算或修改人物，不检查授权、地点及待办任务。0 与 UINT32_MAX 均为合法时刻，不充当无事件标志。
 * due_ms == now_ms 表示应立即检查规划条件，不应在条件未变时反复登记立即唤醒；范围错误不能转换为立即重试。
 */
DOMINO_CODE dominoHumanGetSleepTriggerMs(const DominoHumanData* data_ptr, domino_runtime_ms_t now_ms, domino_runtime_ms_t* due_out_ptr);

/** @brief 内部整数衰减 helper；先比较可扣分钟数，避免乘法溢出及无符号减法下溢。 */
static inline uint8_t dominoHumanDecayNeed(uint8_t value, uint32_t minute_count, uint8_t decay_per_minute) {
    if (minute_count == 0U || decay_per_minute == 0U) {
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
 * @note 仅供统一睡眠结算按时间顺序调用；调用者须独占写入，并在收益点之前结算自然变化。
 * 普通调用者使用 dominoHumanSettleNeeds，不能绕过活动槽检查而先结算整段自然变化。
 */
void dominoHumanApplyNaturalNeeds(DominoHumanData* data_ptr, domino_runtime_ms_t now_ms);

/** @brief 同步结算空闲人物的自然需求；三个活动槽均为空时才允许独立调用。
 * @param data_ptr 调用方已取得并独占写入的人物 data，调用期间不得使其失效。
 * @param now_ms 本次结算的累计未冻结现实毫秒；本函数不读取时钟，调用方须保证人物业务时间不倒退。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；需求无效返回 ERR_INVALID_DATA；时刻早于自然检查点返回 ERR_INVALID_PARAM；
 * 活动槽非空返回 ERR_GAME_STATE_INVALID。失败不修改人物。
 * @note 无后台线程或计时登记；有活动任务时必须通过统一行为结算入口保持自然变化与收益的时间顺序。
 */
DOMINO_CODE dominoHumanSettleNeeds(DominoHumanData* data_ptr, domino_runtime_ms_t now_ms);

#endif
