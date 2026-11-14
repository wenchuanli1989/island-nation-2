#include "sleep.h"
DominoTaskDecision dominoSleepTaskDecide(const DominoTask* task, DominoTaskDecisionInput input) {
    if (input.energy >= task->target_energy) {
        if (task->has_execution && task->execution.successful_behavior_count)
            return (DominoTaskDecision){DOMINO_TASK_SUCCEED, 0, true};
        // 自身恢复达到目标时继续至本行为成功；外部补满不能替代自身成功证明。
        if (input.energy_replaced || !task->execution.current_behavior_id)
            return (DominoTaskDecision){task->has_execution ? DOMINO_TASK_CANCEL : DOMINO_TASK_REVOKE, 1, true};
    }
    if (!input.sleep_allowed)
        return (DominoTaskDecision){DOMINO_TASK_WAIT, 2, true};
    return (DominoTaskDecision){task->execution.current_behavior_id || task->plan_count ? DOMINO_TASK_CONTINUE : DOMINO_TASK_REPAIR, 0, false};
}
