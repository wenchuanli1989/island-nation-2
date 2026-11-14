#ifndef DOMINO_HUMAN_TASK_ENTRY_H
#define DOMINO_HUMAN_TASK_ENTRY_H
#include "../entry.h"
typedef enum {
    DOMINO_TASK_CONTINUE,
    DOMINO_TASK_WAIT,
    DOMINO_TASK_REPAIR,
    DOMINO_TASK_SUCCEED,
    DOMINO_TASK_FAIL,
    DOMINO_TASK_CANCEL,
    DOMINO_TASK_REVOKE
} DominoTaskAction;
typedef struct DominoTaskDecision {
    DominoTaskAction action;
    uint32_t reason;
    bool cancel_behavior;
} DominoTaskDecision;
/** @brief 同一边界的必要输入；推进与只读预测共用决策，不借用整个人物。 */
typedef struct DominoTaskDecisionInput {
    domino_runtime_ms_t now_ms;
    uint8_t energy;
    bool sleep_allowed;
    bool dependencies_ready;
    bool completion_ready;
    bool energy_replaced;
} DominoTaskDecisionInput;
typedef struct DominoTaskSummary {
    DominoHumanCheckTime next;
    DOMINO_TASK_TYPE task_key;
    DOMINO_BEHAVIOR_TYPE behavior_key;
    bool running;
    bool holds_behavior;
    bool can_start;
    bool needs_behavior;
    bool needs_arrangement;
    bool success_proof;
} DominoTaskSummary;
/** @brief 只读统一决策：所有效果及行为终态归并后调用；依赖由推进层按拓扑顺序确认。 */
DominoTaskDecision dominoTaskDecide(const DominoTask* task, DominoTaskDecisionInput input);
void dominoTaskApplyDecision(DominoTask* task, DominoTaskDecision decision, domino_runtime_ms_t now);
DominoTaskSummary dominoTaskQuery(const DominoTask* task, const DominoHumanData* data, domino_runtime_ms_t base);
bool dominoTaskSupported(DOMINO_TASK_TYPE type);
bool dominoTaskDependenciesReady(const DominoTask* task, uint8_t stage);
void dominoTaskSettleEffects(DominoTask* task, DominoHumanData* data, domino_runtime_ms_t begin, domino_runtime_ms_t end);
/** @brief 终态与任务事实立即一起提交；重复调用不再归并。 */
void dominoTaskFinishBehavior(DominoTask* task, DOMINO_BEHAVIOR_STATUS status, domino_runtime_ms_t now, uint32_t reason);
void dominoTaskCheckBehavior(DominoTask* task, domino_runtime_ms_t now);
void dominoTaskStartBehavior(DominoTask* task, domino_runtime_ms_t now);
#endif
