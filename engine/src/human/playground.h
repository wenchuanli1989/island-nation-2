#ifndef DOMINO_HUMAN_PLAYGROUND_H
#define DOMINO_HUMAN_PLAYGROUND_H
#include "../queue/message.h"
#include "behavior/entry.h"
#include "task/entry.h"
/** @brief 仅模块内部受控策略和验收入口，不用于生产调度或客户端。 */
bool dominoBehaviorFixtureSupported(DOMINO_BEHAVIOR_TYPE type);
void dominoBehaviorFixtureCalculate(DominoBehavior* value, DominoNeedValues* needs, domino_runtime_ms_t begin, domino_runtime_ms_t end);
bool dominoTaskFixtureSupported(DOMINO_TASK_TYPE type);
DominoTaskDecision dominoTaskFixtureDecide(const DominoTask* task, DominoTaskDecisionInput input);
int dominoHumanAIPlayground(int argc, char** argv);
void dominoHumanPlannerTestFairness(void);
void dominoHumanPlannerTestLeaveCandidate(DominoHuman* human);
void dominoHumanExecutionTestResult(DominoHumanPlanResultMessage* result);
#endif
