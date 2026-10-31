#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_SLEEP_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_SLEEP_H

#include "domino_shared_human.h"

/** @brief 内部入口：累计本行为的有效睡眠进度和整数精力收益。
 * @param data_ptr 已校验且由调用者独占写入的人物 data。
 * @param behavior_ptr 已校验的 ACTIVE 睡眠行为，计划结束不早于开始，有效进度不超过计划区间长度。
 * @param now_ms 不早于行为 accounted_at_ms 的累计未冻结现实毫秒。
 * @note 执行器合并各通道的效果边界，先统一结算自然需求，再推进各任务；本函数不重复结算自然需求。
 * 有效睡眠达到目标后不延长收益；完成任务、执行版本和活动槽由调用者维护。
 * 本函数不读取时钟、不分配内存、不返回失败；达到目标时仅将行为记为 DONE，并以 now_ms 记录处理完成时刻。
 */
void dominoHumanApplySleep(DominoHumanData* data_ptr, DominoBehavior* behavior_ptr, domino_runtime_ms_t now_ms);

#endif
