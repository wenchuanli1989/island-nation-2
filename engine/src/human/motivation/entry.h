#ifndef DOMINO_ENGINE_HUMAN_MOTIVATION_ENTRY_H
#define DOMINO_ENGINE_HUMAN_MOTIVATION_ENTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_human.h"

/** @file entry.h
 * @brief 动机：查询有效动机、记录所属任务结束和读取最近成功明细。
 * @note 动机只保存方向与历史，是否行动由规划器判断，执行器不复评。终结后的动机仍为已启动任务的成功计次，但不恢复有效。
 */

/** @brief 人物持有的指定类型有效动机，没有返回空指针。 */
DominoMotivation* dominoHumanFindActiveMotivation(const DominoHumanData* data_ptr, uint8_t type);

/** @brief 人物是否持有任何有效动机。 */
bool dominoHumanHasActiveMotivation(const DominoHumanData* data_ptr);

/** @brief 所属任务结束时更新动机：结束本轮需求；成功则计次并追加最近明细，否则记录失败原因。
 * @pre 任务已进入执行终态或已撤销；同一任务只结束一次，因此不会重复计次。
 */
void dominoHumanMotivationRecordTaskEnded(DominoMotivation* motivation_ptr, const DominoTask* task_ptr);

/** @brief 按时间顺序取第 index 条最近成功明细（0 为最早）。 */
DominoMotivationHistory dominoHumanMotivationHistoryAt(const DominoMotivation* motivation_ptr, uint8_t index);

#endif
