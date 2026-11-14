#ifndef DOMINO_HUMAN_DATA_H
#define DOMINO_HUMAN_DATA_H
#include "domino_engine.h"
#include "entry.h"
/** @brief 写锁内使用；调用者已按旧条件共同结算，实际变化后留下重评依据。 */
void dominoHumanApplySleepCondition(DominoHuman* human, bool allowed);
void dominoHumanApplyEnergy(DominoHuman* human, uint8_t energy);
/** @brief 短写锁内的显示更新，不结算、不触发 AI；复制独立响应。 */
void dominoHumanSetDisplayName(uint64_t client_id, const DominoNpcRequest* request, DominoNpcResult* result);
#endif
