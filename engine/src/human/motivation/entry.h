#ifndef DOMINO_ENGINE_HUMAN_MOTIVATION_ENTRY_H
#define DOMINO_ENGINE_HUMAN_MOTIVATION_ENTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_human.h"

/** @file entry.h
 * @brief 动机统一接口：查询有效动机、记录所属任务结束（成功实施或失败依据）及读取最近明细。
 * @note 动机只保存方向与历史，不表达当前是否需要行动；动机的建立与终结、是否为它安排工作及何时再规划都由规划器判断，
 * 执行器不复评动机。实施次数与最近 10 条明细只由已审核成功的所属任务更新；终结后的动机仍接受已启动任务的成功计次，但不恢复有效。
 */

/** @brief 在人物持有的动机中查找指定类型的有效动机；没有返回空指针。 */
DominoMotivation* dominoHumanFindActiveMotivation(const DominoHumanData* data_ptr, uint8_t type);

/** @brief 人物是否持有任何有效动机。 */
bool dominoHumanHasActiveMotivation(const DominoHumanData* data_ptr);

/** @brief 所属任务结束时在同次写入中更新动机：成功计次并追加最近明细，失败／取消只归并原因；结束本轮。
 * @pre task_ptr 已进入执行终态或已依法撤销；同一任务的终态转换只发生一次，因此不会重复计次。
 * @note 累计次数溢出致命退出。
 */
void dominoHumanMotivationRecordTaskEnded(DominoMotivation* motivation_ptr, const DominoTask* task_ptr);

/** @brief 按时间顺序取第 index 条最近明细（0 为最早）。 */
DominoMotivationHistory dominoHumanMotivationHistoryAt(const DominoMotivation* motivation_ptr, uint8_t index);

#endif
