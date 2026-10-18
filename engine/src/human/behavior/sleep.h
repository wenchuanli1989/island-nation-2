#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_SLEEP_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_SLEEP_H

#include "domino_shared_human.h"

/** @brief 内部入口：按有效睡眠的整数收益边界，交错结算自然需求和精力恢复。
 * @param data_ptr 已校验且由调用者独占写入的人物 data。
 * @param behavior_ptr 已校验的 ACTIVE 睡眠行为，有效进度严格小于目标。
 * @param now_ms 不早于行为 accounted_at_ms 的累计未冻结现实毫秒。
 * @note 调用者须预检指针、需求范围、行为状态及时间：自然结算检查点不晚于旧 accounted_at_ms，
 * 两者之差不足一个完整现实分钟。完成任务、执行版本和活动槽由调用者在同次提交中维护。
 * 同刻先结算自然变化再恢复精力；有效睡眠达到目标后只继续自然衰减，不延长睡眠收益。
 * 本函数不读取时钟、不分配内存、不返回失败；达到目标时仅将行为记为 DONE，并以 now_ms 记录处理完成时刻。
 */
void dominoHumanApplySleep(DominoHumanData* data_ptr, DominoBehavior* behavior_ptr, domino_runtime_ms_t now_ms);

#endif
