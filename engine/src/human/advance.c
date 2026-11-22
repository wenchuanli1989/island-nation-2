#include "advance.h"

#include <inttypes.h>

#include "../logger/entry.h"
#include "behavior/entry.h"
#include "domino_shared_common.h"
#include "entry.h"
#include "motivation/entry.h"
#include "needs.h"
#include "rules.h"
#include "state.h"
#include "store.h"

/** @brief 待处理外部信息对规划的需要。 */
typedef enum {
    DOMINO_HUMAN_STIMULUS_NEED_NONE = 0,  ///< 没有待处理外部信息。
    DOMINO_HUMAN_STIMULUS_NEED_INSERT,    ///< 运行任务接受插入：需要为它规划插入的辅助步骤。
    DOMINO_HUMAN_STIMULUS_NEED_TASK,      ///< 没有运行任务或运行任务选择完全让位：需要回应任务。
} DOMINO_HUMAN_STIMULUS_NEED;

/** @brief 一段共同区间的结算视图：需求局部值和运行任务各通道当前行为（真实记录或局部副本）。
 * @note 真实结算与只读预测共用同一视图、同一分段选择和同一基础计算，只是写入目标不同。
 */
typedef struct HumanSegmentView {
    DominoHumanNeedValues needs;
    DominoBehavior* behavior_list[DOMINO_BEHAVIOR_CHANNEL_COUNT];
    DominoHumanConditionView condition;
    bool natural_boundary_flag;  ///< 有任务或行为需要按自然变化判定时，按整分钟分段。
} HumanSegmentView;

static DominoHumanConditionView conditionOf(const DominoHumanData* data_ptr) {
    return (DominoHumanConditionView){.sleep_forbidden_flag = (data_ptr->ai_flags & DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN) != 0U};
}

static bool hasLiveTask(const DominoHumanData* data_ptr) {
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        if (dominoTaskIsLive(dominoGetTaskById(data_ptr->task_id_list[index]))) {
            return true;
        }
    }
    return false;
}

/** @brief 人物的运行任务，没有返回空指针。 */
static DominoTask* getActiveTask(const DominoHumanData* data_ptr) {
    return data_ptr->active_task_id != 0U ? dominoGetTaskById(data_ptr->active_task_id) : nullptr;
}

/** @brief 建立视图；local_list 非空时复制行为到局部副本（预测），否则直接引用池内记录（真实结算）。 */
static void loadSegmentView(HumanSegmentView* view_ptr, const DominoHumanData* data_ptr, DominoBehavior* local_list) {
    view_ptr->needs = dominoHumanReadNeeds(data_ptr);
    view_ptr->condition = conditionOf(data_ptr);
    const DominoTask* active_ptr = getActiveTask(data_ptr);
    bool behavior_flag = false;
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const domino_behavior_id_t behavior_id = active_ptr != nullptr ? active_ptr->current_behavior_id_list[channel] : 0U;
        DominoBehavior* behavior_ptr = behavior_id != 0U ? dominoGetBehaviorById(behavior_id) : nullptr;
        if (behavior_ptr != nullptr && local_list != nullptr) {
            local_list[channel] = *behavior_ptr;
            behavior_ptr = &local_list[channel];
        }
        view_ptr->behavior_list[channel] = behavior_ptr;
        behavior_flag = behavior_flag || behavior_ptr != nullptr;
    }
    view_ptr->natural_boundary_flag = behavior_flag || hasLiveTask(data_ptr);
}

/** @brief 选出晚于 begin 且不晚于 target 的下一个边界：自然变化、行为效果或完成、任务截止点。 */
static domino_runtime_ms_t selectBoundary(const HumanSegmentView* view_ptr, const DominoHumanData* data_ptr, domino_runtime_ms_t begin_ms,
                                          domino_runtime_ms_t target_ms) {
    uint64_t end_ms = target_ms;
    if (view_ptr->natural_boundary_flag) {
        const uint64_t tick_ms = (uint64_t)begin_ms + dominoHumanMsUntilNeedTick(begin_ms);
        end_ms = tick_ms < end_ms ? tick_ms : end_ms;
    }
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const DominoBehavior* behavior_ptr = view_ptr->behavior_list[channel];
        if (behavior_ptr == nullptr) {
            continue;
        }
        const DominoBehaviorCalcOutput output = dominoHumanCalcBehavior(behavior_ptr, &view_ptr->condition, 0U);
        if (output.has_boundary) {
            const uint64_t boundary_ms = (uint64_t)begin_ms + output.next_boundary_ms;
            end_ms = boundary_ms < end_ms ? boundary_ms : end_ms;
        }
    }
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        const DominoTask* task_ptr = dominoGetTaskById(data_ptr->task_id_list[index]);
        if (dominoTaskIsLive(task_ptr) && (task_ptr->flags & DOMINO_TASK_FLAG_DEADLINE_VALID) != 0U && task_ptr->deadline_ms > begin_ms &&
            task_ptr->deadline_ms < end_ms) {
            end_ms = task_ptr->deadline_ms;
        }
    }
    return (domino_runtime_ms_t)end_ms;
}

/** @brief 结算区间 [begin_ms, end_ms]：先结算一次自然需求，再按主、辅 1、辅 2 的顺序结算运行任务已启动行为的效果和进度。 */
static void applySegment(HumanSegmentView* view_ptr, domino_runtime_ms_t begin_ms, domino_runtime_ms_t end_ms) {
    if (end_ms == begin_ms) {
        return;
    }
    const domino_runtime_ms_t span_ms = end_ms - begin_ms;
    view_ptr->needs = dominoHumanCalcNaturalNeeds(&view_ptr->needs, begin_ms, end_ms);
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        DominoBehavior* behavior_ptr = view_ptr->behavior_list[channel];
        if (behavior_ptr == nullptr) {
            continue;
        }
        const DominoBehaviorCalcOutput output = dominoHumanCalcBehavior(behavior_ptr, &view_ptr->condition, span_ms);
        behavior_ptr->effective_progress_ms = output.progress_ms;
        view_ptr->needs.energy = dominoHumanApplyEnergyDelta(view_ptr->needs.energy, output.energy_delta);
    }
}

DominoTaskDecisionInput dominoHumanBuildDecisionInput(const DominoHuman* human_ptr, const DominoTask* task_ptr, domino_runtime_ms_t now_ms) {
    DominoTaskDecisionInput input = {
        .task_ptr = task_ptr,
        .now_ms = now_ms,
        .energy = human_ptr->data->energy,
        .condition = conditionOf(human_ptr->data),
    };
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const domino_behavior_id_t behavior_id = task_ptr->current_behavior_id_list[channel];
        input.behavior_list[channel] = behavior_id != 0U ? dominoGetBehaviorById(behavior_id) : nullptr;
    }
    return input;
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 行为终态与同一任务内的行为依赖                                                                                     */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 来源步骤结束：把结果归并到尚未确定的依赖项。伴随依赖的来源一旦结束即失效。 */
static void mergeDependencyResult(DominoBehaviorDependency* dependency_list, uint32_t step_seq, uint8_t status) {
    for (uint8_t index = 0U; index < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; index++) {
        DominoBehaviorDependency* dependency_ptr = &dependency_list[index];
        if (dependency_ptr->kind == DOMINO_BEHAVIOR_DEPENDENCY_NONE || dependency_ptr->step_seq != step_seq ||
            dependency_ptr->state != DOMINO_BEHAVIOR_DEPENDENCY_PENDING) {
            continue;
        }
        dependency_ptr->state = dependency_ptr->kind == DOMINO_BEHAVIOR_DEPENDENCY_WHILE || status != DOMINO_BEHAVIOR_STATUS_SUCCEEDED
                                    ? DOMINO_BEHAVIOR_DEPENDENCY_BROKEN
                                    : DOMINO_BEHAVIOR_DEPENDENCY_SATISFIED;
    }
}

/** @brief 同次写入中把来源步骤的终态归并到本任务的当前行为和未来步骤；依赖只存在于同一任务内。 */
static void propagateStepResult(const DominoTask* task_ptr, uint32_t step_seq, uint8_t status) {
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const domino_behavior_id_t behavior_id = task_ptr->current_behavior_id_list[channel];
        if (behavior_id != 0U) {
            mergeDependencyResult(dominoGetBehaviorById(behavior_id)->dependency_list, step_seq, status);
        }
    }
    for (uint8_t index = 0U; index < task_ptr->plan_item_count; index++) {
        mergeDependencyResult(task_ptr->plan_item_list[index].dependency_list, step_seq, status);
    }
}

/** @brief 行为由非终态进入终态：主行为同次归并有效进度、成功证明或失败原因；清除当前引用，归并依赖方，保留为最近终态记录。
 * @note 插入行为和其他辅助行为不计入任务进度和成功证明。回收上一条终态记录会移动行为池，调用后不得使用任何行为指针。
 */
static void commitBehaviorTerminal(DominoTask* task_ptr, uint8_t channel, uint8_t status, uint8_t reason, domino_runtime_ms_t now_ms) {
    DominoBehavior* behavior_ptr = dominoGetBehaviorById(task_ptr->current_behavior_id_list[channel]);
    behavior_ptr->status = status;
    behavior_ptr->ended_at_ms = now_ms;
    behavior_ptr->end_reason = reason;
    if (channel == DOMINO_BEHAVIOR_CHANNEL_MAIN) {
        if (behavior_ptr->effective_progress_ms > UINT32_MAX - task_ptr->progress_ms) {
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "task=%u merged progress overflows", task_ptr->task_id);
        }
        task_ptr->progress_ms += behavior_ptr->effective_progress_ms;
        uint16_t* count_ptr = &task_ptr->main_cancelled_count;
        if (status == DOMINO_BEHAVIOR_STATUS_SUCCEEDED) {
            count_ptr = &task_ptr->main_succeeded_count;
        } else if (status == DOMINO_BEHAVIOR_STATUS_FAILED) {
            count_ptr = &task_ptr->main_failed_count;
        }
        if (*count_ptr == UINT16_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "task=%u behavior result count overflows", task_ptr->task_id);
        }
        (*count_ptr)++;
    }
    task_ptr->last_behavior_end_reason = reason;
    task_ptr->current_behavior_id_list[channel] = 0U;
    const domino_behavior_id_t behavior_id = behavior_ptr->behavior_id;
    propagateStepResult(task_ptr, behavior_ptr->step_seq, status);
    const domino_behavior_id_t previous_id = task_ptr->last_behavior_id;
    task_ptr->last_behavior_id = behavior_id;
    if (previous_id != 0U) {
        dominoHumanStoreReclaimBehavior(task_ptr, previous_id);
    }
}

/** @brief 行为在当前事实下的结论；status 为 ACTIVE 表示继续。 */
typedef struct BehaviorOutcome {
    uint8_t status;
    uint8_t reason;
} BehaviorOutcome;

/** @brief 先看依赖再看本类型判定：伴随来源已结束则取消，完成依赖失效则失败；自身已满足但完成依赖未到时继续等待。 */
static BehaviorOutcome judgeBehavior(const DominoBehavior* behavior_ptr) {
    bool complete_pending_flag = false;
    for (uint8_t index = 0U; index < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; index++) {
        const DominoBehaviorDependency* dependency_ptr = &behavior_ptr->dependency_list[index];
        if (dependency_ptr->kind == DOMINO_BEHAVIOR_DEPENDENCY_WHILE && dependency_ptr->state == DOMINO_BEHAVIOR_DEPENDENCY_BROKEN) {
            return (BehaviorOutcome){.status = DOMINO_BEHAVIOR_STATUS_CANCELLED, .reason = DOMINO_END_REASON_DEPENDENCY_BROKEN};
        }
        if (dependency_ptr->kind == DOMINO_BEHAVIOR_DEPENDENCY_COMPLETE) {
            if (dependency_ptr->state == DOMINO_BEHAVIOR_DEPENDENCY_BROKEN) {
                return (BehaviorOutcome){.status = DOMINO_BEHAVIOR_STATUS_FAILED, .reason = DOMINO_END_REASON_DEPENDENCY_BROKEN};
            }
            complete_pending_flag = complete_pending_flag || dependency_ptr->state == DOMINO_BEHAVIOR_DEPENDENCY_PENDING;
        }
    }
    const uint8_t result = dominoHumanGetBehaviorRules(behavior_ptr->type)->judge(behavior_ptr);
    if (result == DOMINO_BEHAVIOR_STATUS_SUCCEEDED && !complete_pending_flag) {
        return (BehaviorOutcome){.status = DOMINO_BEHAVIOR_STATUS_SUCCEEDED, .reason = DOMINO_END_REASON_COMPLETED};
    }
    if (result == DOMINO_BEHAVIOR_STATUS_FAILED) {
        return (BehaviorOutcome){.status = DOMINO_BEHAVIOR_STATUS_FAILED, .reason = DOMINO_END_REASON_CONDITION_LOST};
    }
    return (BehaviorOutcome){.status = DOMINO_BEHAVIOR_STATUS_ACTIVE};
}

/** @brief 判定并提交本边界已成立的行为终态，每次提交后归并依赖，直到没有新的终态；最多三个并行行为，轮数有界。
 * @return 是否提交了任何终态。
 */
static bool resolveTaskBehaviors(DominoTask* task_ptr, domino_runtime_ms_t now_ms) {
    bool changed_flag = false;
    for (uint8_t pass = 0U; pass <= DOMINO_BEHAVIOR_CHANNEL_COUNT; pass++) {
        bool progress_flag = false;
        for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
            const domino_behavior_id_t behavior_id = task_ptr->current_behavior_id_list[channel];
            if (behavior_id == 0U) {
                continue;
            }
            const BehaviorOutcome outcome = judgeBehavior(dominoGetBehaviorById(behavior_id));
            if (outcome.status != DOMINO_BEHAVIOR_STATUS_ACTIVE) {
                commitBehaviorTerminal(task_ptr, channel, outcome.status, outcome.reason, now_ms);
                progress_flag = true;
            }
        }
        if (!progress_flag) {
            break;
        }
        changed_flag = true;
    }
    return changed_flag;
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 任务决策应用与边界处理                                                                                             */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 从待执行队列移除并释放运行任务位置；队列实际变化时推进运行期队列版本。 */
static void releaseTaskPosition(DominoHuman* human_ptr, const DominoTask* task_ptr) {
    DominoHumanData* data_ptr = human_ptr->data;
    if (dominoHumanTaskQueueRemove(data_ptr, task_ptr->task_id) && g_domino_human_runtime_list != nullptr) {
        dominoHumanRuntimeOf(human_ptr)->queue_version++;
    }
    if (data_ptr->active_task_id == task_ptr->task_id) {
        data_ptr->active_task_id = 0U;
    }
}

/** @brief 终结 channel_mask 所列通道的当前行为；keep_inserted 时保留插入的辅助行为。返回是否终结了任何行为。 */
static bool endCurrentBehaviors(DominoTask* task_ptr, uint8_t channel_mask, bool keep_inserted, uint8_t status, uint8_t reason,
                                domino_runtime_ms_t now_ms) {
    bool changed_flag = false;
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const domino_behavior_id_t behavior_id = task_ptr->current_behavior_id_list[channel];
        if ((channel_mask & (1U << channel)) == 0U || behavior_id == 0U ||
            (keep_inserted && (dominoGetBehaviorById(behavior_id)->flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U)) {
            continue;
        }
        commitBehaviorTerminal(task_ptr, channel, status, reason, now_ms);
        changed_flag = true;
    }
    return changed_flag;
}

/** @brief 清理未来步骤；keep_inserted 时保留插入的辅助步骤及其先后。未执行步骤不创建行为记录。 */
static void clearPlan(DominoTask* task_ptr, bool keep_inserted) {
    if (task_ptr->plan_item_count == 0U) {
        return;
    }
    if (!keep_inserted) {
        dominoHumanStoreReplacePlan(task_ptr, nullptr, 0U);
        return;
    }
    DominoBehaviorPlanItem kept_list[DOMINO_TASK_STEP_COUNT_MAX];
    uint8_t kept_count = 0U;
    for (uint8_t index = 0U; index < task_ptr->plan_item_count; index++) {
        if ((task_ptr->plan_item_list[index].flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U) {
            kept_list[kept_count++] = task_ptr->plan_item_list[index];
        }
    }
    if (kept_count != task_ptr->plan_item_count) {
        dominoHumanStoreReplacePlan(task_ptr, kept_list, kept_count);
    }
}

bool dominoHumanApplyTaskDecision(DominoHuman* human_ptr, DominoTask* task_ptr, DominoTaskDecision decision, domino_runtime_ms_t now_ms) {
    switch (decision.kind) {
        case DOMINO_TASK_DECISION_CONTINUE:
            return false;
        case DOMINO_TASK_DECISION_REPAIR:
            return endCurrentBehaviors(task_ptr, decision.channel_mask, false, decision.behavior_end_status, decision.reason, now_ms);
        case DOMINO_TASK_DECISION_SUCCEED: {
            const DominoTaskDecisionInput input = dominoHumanBuildDecisionInput(human_ptr, task_ptr, now_ms);
            if (dominoHumanTaskHasInsertedWork(&input)) {
                if ((task_ptr->flags & DOMINO_TASK_FLAG_COMPLETION_REACHED) != 0U) {
                    return false;
                }
                /* 完成条件已成立：自身其余行为合法结束、自身步骤清理，插入的辅助行为继续到自行结束后再提交成功。 */
                endCurrentBehaviors(task_ptr, DOMINO_BEHAVIOR_CHANNEL_MASK_ALL, true, DOMINO_BEHAVIOR_STATUS_CANCELLED, DOMINO_END_REASON_TASK_ENDED,
                                    now_ms);
                clearPlan(task_ptr, true);
                task_ptr->flags |= DOMINO_TASK_FLAG_COMPLETION_REACHED;
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                                  "task completion reached human=%" PRIu32 " task=%" PRIu32 " waiting inserted behaviors at_ms=%" PRIu32,
                                  human_ptr->human_id, task_ptr->task_id, now_ms);
                return true;
            }
            /* 任务获准结束时仍在执行的行为记为 CANCELLED，不能为满足成功条件改记成功。 */
            endCurrentBehaviors(task_ptr, DOMINO_BEHAVIOR_CHANNEL_MASK_ALL, false, DOMINO_BEHAVIOR_STATUS_CANCELLED, DOMINO_END_REASON_TASK_ENDED,
                                now_ms);
            task_ptr->exec_status = DOMINO_TASK_EXEC_STATUS_SUCCEEDED;
            task_ptr->ended_at_ms = now_ms;
            break;
        }
        case DOMINO_TASK_DECISION_FAIL:
        case DOMINO_TASK_DECISION_CANCEL:
            endCurrentBehaviors(task_ptr, DOMINO_BEHAVIOR_CHANNEL_MASK_ALL, false, DOMINO_BEHAVIOR_STATUS_CANCELLED, DOMINO_END_REASON_TASK_ENDED,
                                now_ms);
            task_ptr->exec_status = decision.kind == DOMINO_TASK_DECISION_FAIL ? DOMINO_TASK_EXEC_STATUS_FAILED : DOMINO_TASK_EXEC_STATUS_CANCELLED;
            task_ptr->ended_at_ms = now_ms;
            break;
        case DOMINO_TASK_DECISION_REVOKE:
            /* 未启动工作依法撤销，不制造执行终态；任务记录随后回收，task_id 不再复用。 */
            task_ptr->flags |= DOMINO_TASK_FLAG_REVOKED;
            break;
        default:
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "invalid task decision kind=%u task=%u", decision.kind, task_ptr->task_id);
    }
    task_ptr->end_reason = decision.reason;
    clearPlan(task_ptr, false);
    releaseTaskPosition(human_ptr, task_ptr);
    DominoMotivation* motivation_ptr = dominoGetMotivationById(task_ptr->motivation_id);
    if (motivation_ptr != nullptr) {
        dominoHumanMotivationRecordTaskEnded(motivation_ptr, task_ptr);
    }
    /* 任务进入执行终态或被撤销：动机和任务规划器都需要按新的任务集合再看一次。 */
    dominoHumanMarkPlanPending(human_ptr, DOMINO_HUMAN_PLAN_MASK_ALL);
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                      "task ended human=%" PRIu32 " task=%" PRIu32 " decision=%u reason=%u at_ms=%" PRIu32, human_ptr->human_id, task_ptr->task_id,
                      decision.kind, decision.reason, now_ms);
    return true;
}

/** @brief 完成条件已成立的任务等插入行为全部结束后提交成功；其他任务调用统一决策。 */
static DominoTaskDecision decideTask(const DominoHuman* human_ptr, const DominoTask* task_ptr, domino_runtime_ms_t now_ms) {
    const DominoTaskDecisionInput input = dominoHumanBuildDecisionInput(human_ptr, task_ptr, now_ms);
    if ((task_ptr->flags & DOMINO_TASK_FLAG_COMPLETION_REACHED) != 0U) {
        return dominoHumanTaskHasInsertedWork(&input)
                   ? (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_CONTINUE}
                   : (DominoTaskDecision){.kind = DOMINO_TASK_DECISION_SUCCEED, .reason = DOMINO_END_REASON_COMPLETED};
    }
    return dominoHumanGetTaskRules(task_ptr->type)->decide(&input);
}

/** @brief 在边界时刻先判定并提交运行任务的行为终态及依赖归并，再对每项任务应用统一决策；直到本边界的连带变化全部处理完。
 * @note 任务之间没有依赖，单项结果不影响其他任务；每轮有变化至少终结一个行为或任务，或记下一次完成条件，轮数有界。
 */
static void resolveBoundaryAt(DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    DominoHumanData* data_ptr = human_ptr->data;
    const uint32_t pass_max = (2U * (uint32_t)data_ptr->task_count) + (2U * DOMINO_BEHAVIOR_CHANNEL_COUNT) + 2U;
    for (uint32_t pass = 0U; pass <= pass_max; pass++) {
        bool changed_flag = false;
        DominoTask* active_ptr = getActiveTask(data_ptr);
        if (active_ptr != nullptr) {
            changed_flag = resolveTaskBehaviors(active_ptr, now_ms);
        }
        for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
            DominoTask* task_ptr = dominoGetTaskById(data_ptr->task_id_list[index]);
            if (!dominoTaskIsLive(task_ptr)) {
                continue;
            }
            const DominoTaskDecision decision = decideTask(human_ptr, task_ptr, now_ms);
            changed_flag = dominoHumanApplyTaskDecision(human_ptr, task_ptr, decision, now_ms) || changed_flag;
        }
        if (!changed_flag) {
            return;
        }
    }
}

void dominoHumanSettle(DominoHuman* human_ptr, domino_runtime_ms_t target_ms) {
    DominoHumanData* data_ptr = human_ptr->data;
    if (target_ms < data_ptr->settled_at_ms) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "human=%" PRIu32 " settlement target precedes cursor", human_ptr->human_id);
    }
    while (data_ptr->settled_at_ms < target_ms) {
        HumanSegmentView view;
        loadSegmentView(&view, data_ptr, nullptr);
        const domino_runtime_ms_t begin_ms = data_ptr->settled_at_ms;
        const domino_runtime_ms_t end_ms = selectBoundary(&view, data_ptr, begin_ms, target_ms);
        applySegment(&view, begin_ms, end_ms);
        dominoHumanWriteNeeds(data_ptr, &view.needs);
        resolveBoundaryAt(human_ptr, end_ms);
        data_ptr->settled_at_ms = end_ms;
    }
}

void dominoHumanResolveBoundary(DominoHuman* human_ptr) {
    resolveBoundaryAt(human_ptr, human_ptr->data->settled_at_ms);
}

void dominoHumanSettleAll(domino_runtime_ms_t now_ms) {
    for (size_t index = 0U; index < kv_size(g_domino_human_list); index++) {
        dominoHumanSettle(&kv_A(g_domino_human_list, index), now_ms);
    }
}

/** @brief 待处理外部信息需要哪种规划（DOMINO_HUMAN_STIMULUS_NEED）：由运行任务的应激规则决定接受插入还是让位。 */
static uint8_t getStimulusNeed(const DominoHuman* human_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    if (data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE) {
        return DOMINO_HUMAN_STIMULUS_NEED_NONE;
    }
    const DominoTask* active_ptr = getActiveTask(data_ptr);
    if (active_ptr == nullptr) {
        return DOMINO_HUMAN_STIMULUS_NEED_TASK;
    }
    const DominoTaskDecisionInput input = dominoHumanBuildDecisionInput(human_ptr, active_ptr, data_ptr->settled_at_ms);
    return dominoHumanGetTaskRules(active_ptr->type)->react(&input, data_ptr->stimulus_kind) == DOMINO_TASK_REACTION_INSERT
               ? DOMINO_HUMAN_STIMULUS_NEED_INSERT
               : DOMINO_HUMAN_STIMULUS_NEED_TASK;
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 只读预测                                                                                                           */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 预测局部状态在边界 t 是否出现需要重新检查的执行事实：行为结束或任务决策变化。 */
static bool predictBoundaryHasEvent(const DominoHuman* human_ptr, const HumanSegmentView* view_ptr, domino_runtime_ms_t now_ms) {
    const DominoHumanData* data_ptr = human_ptr->data;
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const DominoBehavior* behavior_ptr = view_ptr->behavior_list[channel];
        if (behavior_ptr != nullptr && judgeBehavior(behavior_ptr).status != DOMINO_BEHAVIOR_STATUS_ACTIVE) {
            return true;
        }
    }
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        const DominoTask* task_ptr = dominoGetTaskById(data_ptr->task_id_list[index]);
        if (!dominoTaskIsLive(task_ptr) || (task_ptr->flags & DOMINO_TASK_FLAG_COMPLETION_REACHED) != 0U) {
            /* 等待插入行为的任务只在插入行为结束时变化，已由行为判定覆盖。 */
            continue;
        }
        DominoTaskDecisionInput input = dominoHumanBuildDecisionInput(human_ptr, task_ptr, now_ms);
        input.energy = view_ptr->needs.energy;
        if (task_ptr->task_id == data_ptr->active_task_id) {
            for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
                input.behavior_list[channel] = view_ptr->behavior_list[channel];
            }
        }
        if (dominoHumanGetTaskRules(task_ptr->type)->decide(&input).kind != DOMINO_TASK_DECISION_CONTINUE) {
            return true;
        }
    }
    return false;
}

/** @brief 没有推进中的行为时只剩自然衰减和截止点：直接取最早的截止点；需求达到阈值的检查时机由任务规划的下次规划时刻提供。 */
static bool predictWithoutBehavior(const DominoHuman* human_ptr, domino_runtime_ms_t* due_ms_out_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    uint64_t due_ms = UINT64_MAX;
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        const DominoTask* task_ptr = dominoGetTaskById(data_ptr->task_id_list[index]);
        if (dominoTaskIsLive(task_ptr) && (task_ptr->flags & DOMINO_TASK_FLAG_DEADLINE_VALID) != 0U &&
            task_ptr->deadline_ms > data_ptr->settled_at_ms && task_ptr->deadline_ms < due_ms) {
            due_ms = task_ptr->deadline_ms;
        }
    }
    /* 没有截止点时不需要检查；截止点均来自 32 位业务毫秒。 */
    if (due_ms > UINT32_MAX) {
        return false;
    }
    *due_ms_out_ptr = (domino_runtime_ms_t)due_ms;
    return true;
}

bool dominoHumanPredictNextCheck(const DominoHuman* human_ptr, domino_runtime_ms_t* due_ms_out_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    DominoBehavior local_list[DOMINO_BEHAVIOR_CHANNEL_COUNT];
    HumanSegmentView view;
    loadSegmentView(&view, data_ptr, local_list);
    bool progressing_flag = false;
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        progressing_flag = progressing_flag || (view.behavior_list[channel] != nullptr &&
                                                dominoHumanCalcBehavior(view.behavior_list[channel], &view.condition, 0U).has_boundary);
    }
    if (!progressing_flag) {
        return predictWithoutBehavior(human_ptr, due_ms_out_ptr);
    }
    domino_runtime_ms_t now_ms = data_ptr->settled_at_ms;
    for (uint32_t step = 0U; step < DOMINO_HUMAN_PREDICT_STEP_MAX; step++) {
        const domino_runtime_ms_t end_ms = selectBoundary(&view, data_ptr, now_ms, UINT32_MAX);
        if (end_ms == now_ms) {
            return false;
        }
        applySegment(&view, now_ms, end_ms);
        now_ms = end_ms;
        if (predictBoundaryHasEvent(human_ptr, &view, now_ms)) {
            break;
        }
    }
    /* 步数用尽时返回已推进到的边界，届时由正式推进复查。 */
    *due_ms_out_ptr = now_ms;
    return true;
}

bool dominoHumanPredictNeedsAt(const DominoHuman* human_ptr, domino_runtime_ms_t target_ms, DominoHumanNeedValues* values_out_ptr,
                               uint32_t* work_count_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    DominoBehavior local_list[DOMINO_BEHAVIOR_CHANNEL_COUNT];
    HumanSegmentView view;
    loadSegmentView(&view, data_ptr, local_list);
    domino_runtime_ms_t now_ms = data_ptr->settled_at_ms;
    for (uint32_t step = 0U; step < DOMINO_HUMAN_PREDICT_STEP_MAX && now_ms < target_ms; step++) {
        const domino_runtime_ms_t end_ms = selectBoundary(&view, data_ptr, now_ms, target_ms);
        applySegment(&view, now_ms, end_ms);
        now_ms = end_ms;
        (*work_count_ptr)++;
    }
    *values_out_ptr = view.needs;
    return now_ms >= target_ms;
}

/** @brief 局部视图中是否还有按自身进度产生效果或完成边界的行为。 */
static bool viewHasProgressingBehavior(const HumanSegmentView* view_ptr) {
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const DominoBehavior* behavior_ptr = view_ptr->behavior_list[channel];
        if (behavior_ptr != nullptr && dominoHumanCalcBehavior(behavior_ptr, &view_ptr->condition, 0U).has_boundary) {
            return true;
        }
    }
    return false;
}

bool dominoHumanPredictEnergyDropAt(const DominoHuman* human_ptr, domino_runtime_ms_t from_ms, uint8_t threshold, uint32_t step_max,
                                    uint64_t* due_ms_out_ptr, uint32_t* work_count_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    DominoBehavior local_list[DOMINO_BEHAVIOR_CHANNEL_COUNT];
    HumanSegmentView view;
    loadSegmentView(&view, data_ptr, local_list);
    domino_runtime_ms_t now_ms = data_ptr->settled_at_ms;
    uint32_t step = 0U;
    for (; now_ms < from_ms; step++) {
        if (step >= step_max) {
            return false;
        }
        const domino_runtime_ms_t end_ms = selectBoundary(&view, data_ptr, now_ms, from_ms);
        applySegment(&view, now_ms, end_ms);
        now_ms = end_ms;
        (*work_count_ptr)++;
    }
    bool advanced_flag = false;
    for (;;) {
        if (view.needs.energy <= threshold) {
            *due_ms_out_ptr = now_ms;
            return true;
        }
        if (!viewHasProgressingBehavior(&view)) {
            /* 只剩自然衰减：直接计算，不逐分钟推进。 */
            *due_ms_out_ptr = (uint64_t)now_ms + dominoHumanNaturalMsUntilEnergyAtMost(view.needs.energy, now_ms, threshold);
            return true;
        }
        if (step >= step_max) {
            if (!advanced_flag) {
                return false;
            }
            /* 预算内无法确定：给出已推进到的边界作为保守时刻，届时重新规划。 */
            *due_ms_out_ptr = now_ms;
            return true;
        }
        const domino_runtime_ms_t end_ms = selectBoundary(&view, data_ptr, now_ms, UINT32_MAX);
        if (end_ms == now_ms) {
            /* 业务时间上限内不会降到阈值。 */
            *due_ms_out_ptr = UINT64_MAX;
            return true;
        }
        applySegment(&view, now_ms, end_ms);
        now_ms = end_ms;
        step++;
        (*work_count_ptr)++;
        advanced_flag = true;
    }
}

bool dominoHumanCalcSleepDuration(uint8_t energy, domino_runtime_ms_t start_ms, uint8_t goal, uint32_t step_max,
                                  domino_runtime_ms_t* duration_out_ptr, uint32_t* work_count_ptr) {
    DominoBehavior sleep_behavior = {
        .type = DOMINO_BEHAVIOR_TYPE_SLEEP,
        .status = DOMINO_BEHAVIOR_STATUS_ACTIVE,
        .target_effective_ms = UINT32_MAX,
    };
    HumanSegmentView view = {
        .needs = {.energy = energy},
        .behavior_list = {&sleep_behavior, nullptr, nullptr},
        .natural_boundary_flag = true,
    };
    const DominoHumanData empty_data = {0};
    domino_runtime_ms_t now_ms = start_ms;
    for (uint32_t step = 0U; step < step_max; step++) {
        if (view.needs.energy >= goal) {
            *duration_out_ptr = now_ms - start_ms;
            return now_ms > start_ms;
        }
        const domino_runtime_ms_t end_ms = selectBoundary(&view, &empty_data, now_ms, UINT32_MAX);
        applySegment(&view, now_ms, end_ms);
        now_ms = end_ms;
        (*work_count_ptr)++;
    }
    return false;
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 运行期：启动、让位、回收与状态                                                                                     */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 同一任务内其他通道是否已有同类型行为（行为类型互斥）。 */
static bool isBehaviorTypeBusy(const DominoTask* task_ptr, uint8_t channel_index, uint8_t type) {
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const domino_behavior_id_t behavior_id = task_ptr->current_behavior_id_list[channel];
        if (channel != channel_index && behavior_id != 0U && dominoGetBehaviorById(behavior_id)->type == type) {
            return true;
        }
    }
    return false;
}

/** @brief 本任务中步骤序号为 step_seq 的当前未终结行为；没有返回空指针。 */
static const DominoBehavior* findCurrentStep(const DominoTask* task_ptr, uint32_t step_seq) {
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const domino_behavior_id_t behavior_id = task_ptr->current_behavior_id_list[channel];
        if (behavior_id != 0U) {
            const DominoBehavior* behavior_ptr = dominoGetBehaviorById(behavior_id);
            if (behavior_ptr->step_seq == step_seq) {
                return behavior_ptr;
            }
        }
    }
    return nullptr;
}

/** @brief 通道上下一个步骤在计划中的位置；没有返回 -1。同一通道的步骤按计划先后执行。 */
static int32_t findFirstStepOnChannel(const DominoTask* task_ptr, uint8_t channel) {
    for (uint8_t index = 0U; index < task_ptr->plan_item_count; index++) {
        if (task_ptr->plan_item_list[index].channel_index == channel) {
            return index;
        }
    }
    return -1;
}

/** @brief 步骤能否在空闲通道启动：执行条件、行为类型互斥、行为依赖及行为记录池容量；完成条件已成立的任务只启动插入步骤。 */
static bool canStartStep(const DominoHuman* human_ptr, const DominoTask* task_ptr, uint8_t channel, const DominoBehaviorPlanItem* item_ptr) {
    if ((task_ptr->flags & DOMINO_TASK_FLAG_COMPLETION_REACHED) != 0U && (item_ptr->flags & DOMINO_BEHAVIOR_FLAG_INSERTED) == 0U) {
        return false;
    }
    const DominoHumanConditionView condition = conditionOf(human_ptr->data);
    if (!dominoHumanGetBehaviorRules(item_ptr->type)->can_run(&condition) || isBehaviorTypeBusy(task_ptr, channel, item_ptr->type) ||
        dominoHumanStoreBehaviorCount() >= DOMINO_HUMAN_BEHAVIOR_POOL_MAX) {
        return false;
    }
    for (uint8_t index = 0U; index < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; index++) {
        const DominoBehaviorDependency* dependency_ptr = &item_ptr->dependency_list[index];
        switch (dependency_ptr->kind) {
            case DOMINO_BEHAVIOR_DEPENDENCY_START:
                if (dependency_ptr->state != DOMINO_BEHAVIOR_DEPENDENCY_SATISFIED) {
                    return false;
                }
                break;
            case DOMINO_BEHAVIOR_DEPENDENCY_WHILE:
                if (dependency_ptr->state != DOMINO_BEHAVIOR_DEPENDENCY_PENDING || findCurrentStep(task_ptr, dependency_ptr->step_seq) == nullptr) {
                    return false;
                }
                break;
            case DOMINO_BEHAVIOR_DEPENDENCY_COMPLETE:
                if (dependency_ptr->state == DOMINO_BEHAVIOR_DEPENDENCY_BROKEN) {
                    return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
}

/** @brief 消费计划项、分配行为 ID 并建立执行记录；同次写入中设置该通道的当前行为。 */
static void startStep(DominoTask* task_ptr, uint8_t index, domino_runtime_ms_t now_ms) {
    const DominoBehaviorPlanItem item = dominoHumanStoreConsumePlanItem(task_ptr, index);
    DominoBehavior behavior_init = {
        .step_seq = item.step_seq,
        .started_at_ms = now_ms,
        .target_effective_ms = item.target_effective_ms,
        .param = item.param,
        .type = item.type,
        .status = DOMINO_BEHAVIOR_STATUS_ACTIVE,
        .channel_index = item.channel_index,
        .flags = item.flags,
    };
    for (uint8_t dependency_index = 0U; dependency_index < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; dependency_index++) {
        behavior_init.dependency_list[dependency_index] = item.dependency_list[dependency_index];
    }
    task_ptr->current_behavior_id_list[item.channel_index] = dominoHumanStoreAddBehavior(task_ptr, &behavior_init)->behavior_id;
}

/** @brief 按主、辅 1、辅 2 的顺序在空闲通道启动下一步骤；伴随依赖可指向同批刚启动的行为，最多重扫通道数轮。 */
static bool startTaskSteps(const DominoHuman* human_ptr, DominoTask* task_ptr, domino_runtime_ms_t now_ms) {
    bool started_flag = false;
    for (uint8_t pass = 0U; pass < DOMINO_BEHAVIOR_CHANNEL_COUNT; pass++) {
        bool progress_flag = false;
        for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
            if (task_ptr->current_behavior_id_list[channel] != 0U) {
                continue;
            }
            const int32_t index = findFirstStepOnChannel(task_ptr, channel);
            if (index >= 0 && canStartStep(human_ptr, task_ptr, channel, &task_ptr->plan_item_list[index])) {
                startStep(task_ptr, (uint8_t)index, now_ms);
                progress_flag = true;
            }
        }
        if (!progress_flag) {
            break;
        }
        started_flag = true;
    }
    return started_flag;
}

static bool taskHasStartableStep(const DominoHuman* human_ptr, const DominoTask* task_ptr) {
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const int32_t index = task_ptr->current_behavior_id_list[channel] == 0U ? findFirstStepOnChannel(task_ptr, channel) : -1;
        if (index >= 0 && canStartStep(human_ptr, task_ptr, channel, &task_ptr->plan_item_list[index])) {
            return true;
        }
    }
    return false;
}

/** @brief 队列任务成为运行任务：首次启动建立执行部分，沿用原 task_id。 */
static DominoTask* activateQueuedTask(DominoHuman* human_ptr, domino_task_id_t task_id, domino_runtime_ms_t now_ms) {
    DominoHumanData* data_ptr = human_ptr->data;
    DominoTask* task_ptr = dominoGetTaskById(task_id);
    dominoHumanTaskQueueRemove(data_ptr, task_id);
    dominoHumanRuntimeOf(human_ptr)->queue_version++;
    if (task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_NONE) {
        task_ptr->exec_status = DOMINO_TASK_EXEC_STATUS_OPEN;
        task_ptr->started_at_ms = now_ms;
    }
    task_ptr->flags = (uint8_t)(task_ptr->flags & ~DOMINO_TASK_FLAG_PREEMPT);
    data_ptr->active_task_id = task_id;
    return task_ptr;
}

/** @brief 完全让位：同次写入中取消全部当前行为（有效进度归并）、清空未来步骤，任务保持 OPEN 回到待执行队列队首；
 * 恢复时沿用原 task_id、首次开始时刻和累计事实，由行为规划器修复剩余部分。
 */
static void yieldActiveTask(DominoHuman* human_ptr, DominoTask* active_ptr, domino_runtime_ms_t now_ms) {
    DominoHumanData* data_ptr = human_ptr->data;
    endCurrentBehaviors(active_ptr, DOMINO_BEHAVIOR_CHANNEL_MASK_ALL, false, DOMINO_BEHAVIOR_STATUS_CANCELLED, DOMINO_END_REASON_YIELDED, now_ms);
    clearPlan(active_ptr, false);
    data_ptr->active_task_id = 0U;
    dominoHumanTaskQueueInsert(data_ptr, 0U, active_ptr->task_id);
    dominoHumanRuntimeOf(human_ptr)->queue_version++;
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "task yielded human=%" PRIu32 " task=%" PRIu32 " at_ms=%" PRIu32,
                      human_ptr->human_id, active_ptr->task_id, now_ms);
}

bool dominoHumanStartReadyWork(DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    DominoHumanData* data_ptr = human_ptr->data;
    DominoTask* active_ptr = getActiveTask(data_ptr);
    if (data_ptr->task_queue_count != 0U) {
        const DominoTask* head_ptr = dominoGetTaskById(data_ptr->task_queue_list[0]);
        const bool head_ready = head_ptr->plan_item_count != 0U && taskHasStartableStep(human_ptr, head_ptr);
        if (active_ptr != nullptr && head_ready && (head_ptr->flags & DOMINO_TASK_FLAG_PREEMPT) != 0U) {
            /* 回应任务首个行为可以启动时才让位，避免规划期间空等；是否让位由运行任务的应激规则决定。 */
            const DominoTaskDecisionInput input = dominoHumanBuildDecisionInput(human_ptr, active_ptr, now_ms);
            if (dominoHumanGetTaskRules(active_ptr->type)->react(&input, head_ptr->stimulus_kind) == DOMINO_TASK_REACTION_YIELD) {
                const domino_task_id_t head_id = head_ptr->task_id;
                yieldActiveTask(human_ptr, active_ptr, now_ms);
                return startTaskSteps(human_ptr, activateQueuedTask(human_ptr, head_id, now_ms), now_ms);
            }
        }
        if (active_ptr == nullptr && head_ready) {
            /* 新任务只有在没有运行任务、位于队首且首批步骤已经具备启动条件时，才与这些步骤一起开始。 */
            return startTaskSteps(human_ptr, activateQueuedTask(human_ptr, head_ptr->task_id, now_ms), now_ms);
        }
    }
    return active_ptr != nullptr && startTaskSteps(human_ptr, active_ptr, now_ms);
}

/** @brief 人物是否仍有任务（任何状态）归属该动机。 */
static bool motivationHasTask(const DominoHumanData* data_ptr, domino_motivation_id_t motivation_id) {
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        if (dominoGetTaskById(data_ptr->task_id_list[index])->motivation_id == motivation_id) {
            return true;
        }
    }
    return false;
}

void dominoHumanReclaimEnded(DominoHuman* human_ptr) {
    DominoHumanData* data_ptr = human_ptr->data;
    for (uint8_t index = 0U; index < data_ptr->task_count;) {
        const DominoTask* task_ptr = dominoGetTaskById(data_ptr->task_id_list[index]);
        if (dominoTaskIsLive(task_ptr)) {
            index++;
            continue;
        }
        /* 终态或撤销时已归并动机计次并解除队列位置；回收只释放存储。 */
        dominoHumanStoreReclaimTask(data_ptr, index);
    }
    /* 动机已终结且没有任何关联任务时一并回收：覆盖提交终结时和回收最后一个关联任务时两个时机。 */
    for (uint8_t index = 0U; index < data_ptr->motivation_count;) {
        const DominoMotivation* motivation_ptr = dominoGetMotivationById(data_ptr->motivation_id_list[index]);
        if (motivation_ptr->status == DOMINO_MOTIVATION_STATUS_ACTIVE || motivationHasTask(data_ptr, motivation_ptr->motivation_id)) {
            index++;
            continue;
        }
        dominoHumanStoreReclaimMotivation(data_ptr, index);
    }
}

uint8_t dominoHumanComputeState(const DominoHuman* human_ptr) {
    const DominoTask* active_ptr = getActiveTask(human_ptr->data);
    for (uint8_t channel = 0U; active_ptr != nullptr && channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        if (active_ptr->current_behavior_id_list[channel] != 0U) {
            return DOMINO_HUMAN_STATE_NORMAL;
        }
    }
    return DOMINO_HUMAN_STATE_THINKING;
}

bool dominoHumanTaskNeedsBehaviorPlan(const DominoHuman* human_ptr, const DominoTask* task_ptr) {
    if (!dominoTaskIsLive(task_ptr)) {
        return false;
    }
    if (task_ptr->task_id == human_ptr->data->active_task_id && getStimulusNeed(human_ptr) == DOMINO_HUMAN_STIMULUS_NEED_INSERT) {
        return true;
    }
    if ((task_ptr->flags & DOMINO_TASK_FLAG_COMPLETION_REACHED) != 0U) {
        return false;
    }
    const DominoTaskRules* rules_ptr = dominoHumanGetTaskRules(task_ptr->type);
    const DominoTaskDecisionInput input = dominoHumanBuildDecisionInput(human_ptr, task_ptr, human_ptr->data->settled_at_ms);
    return rules_ptr->has_plan_gap(&input) && rules_ptr->can_plan_behavior(&input);
}
