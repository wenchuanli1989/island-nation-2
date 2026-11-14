#ifndef DOMINO_BEHAVIOR_ENTRY_H
#define DOMINO_BEHAVIOR_ENTRY_H
#include "../needs.h"
/** @brief 同步接口均沿用调用者访问范围及共同时间，不读钟、不调度。 */
bool dominoBehaviorPlanValid(const DominoBehaviorPlan* plan);
bool dominoBehaviorCanStart(const DominoBehaviorPlan* plan, const DominoHumanData* data);
/** @brief 纯计算，只改调用者局部值；真实结算也使用这一个算法。 */
void dominoBehaviorCalculate(DominoBehavior* value, DominoNeedValues* needs, domino_runtime_ms_t begin, domino_runtime_ms_t end);
domino_runtime_ms_t dominoBehaviorNextDelta(const DominoBehavior* behavior);
DOMINO_BEHAVIOR_STATUS dominoBehaviorDecide(const DominoBehavior* behavior);
void dominoBehaviorFinish(DominoBehavior* behavior, DOMINO_BEHAVIOR_STATUS status, domino_runtime_ms_t now);
#endif
