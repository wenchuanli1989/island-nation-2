#ifndef DOMINO_HUMAN_ADVANCE_H
#define DOMINO_HUMAN_ADVANCE_H
#include "needs.h"
/** @brief 持写锁或串行结算；所有边界收尾完成才推进唯一游标，不启动后继。 */
void dominoHumanSettle(DominoHuman* human, domino_runtime_ms_t now);
/** @param energy_replaced 本次边界是否包含已经授权的外部精力替换。 */
void dominoHumanCheckTasks(DominoHuman* human, domino_runtime_ms_t now, bool energy_replaced);
void dominoHumanStartReady(DominoHuman* human, domino_runtime_ms_t now);
/** @brief 从通道及互斥事实重建显示状态，不推进业务。 */
void dominoHumanRefreshStatus(DominoHuman* human, domino_runtime_ms_t now);
void dominoHumanRecycle(DominoHuman* human);
/** @brief 只预测必要局部指标/行为，不写事实；复用效果基础计算。 */
bool dominoHumanPredictNeeds(const DominoHuman* human, domino_runtime_ms_t now, uint32_t* budget, DominoNeedValues* result);
void dominoHumanQueueInsert(DominoHuman* human, domino_task_id_t id, uint8_t channel, uint8_t position);
void dominoHumanQueueRemove(DominoHuman* human, domino_task_id_t id, uint8_t channel);
void dominoHumanResolveTaskReferences(DominoHumanData* data, const DominoTask* source, bool succeeded);
#endif
