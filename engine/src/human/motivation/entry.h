#ifndef DOMINO_MOTIVATION_ENTRY_H
#define DOMINO_MOTIVATION_ENTRY_H
#include "../entry.h"
bool dominoMotivationNeedsReassessment(const DominoMotivation* motivation, const DominoHumanData* data);
bool dominoMotivationNeedsTask(const DominoMotivation* motivation, const DominoHumanData* data);
/** @brief 仅由任务首次成功转换调用，同次写入历史；不恢复终态动机。 */
void dominoMotivationRecordSuccess(DominoMotivation* motivation, const DominoTask* task);
void dominoMotivationCloseWork(DominoMotivation* motivation, const DominoTask* task, bool failed, uint32_t reason);
#endif
