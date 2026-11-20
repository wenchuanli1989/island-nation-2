#include "executor.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "../game_timer/entry.h"
#include "../logger/entry.h"
#include "../time/entry.h"
#include "advance.h"
#include "behavior/entry.h"
#include "candidate.h"
#include "domino_shared_common.h"
#include "domino_shared_queue.h"
#include "entry.h"
#include "motivation/entry.h"
#include "planner.h"
#include "rules.h"
#include "state.h"
#include "store.h"
#include "task/entry.h"

/** @file executor.c
 * @brief human 世界的唯一运行期写入者。
 * @note 每条消息的处理顺序：出队 → 取写锁 → 共同结算到本次时刻 → 调用业务接口 → 启动／规划／唤醒调度 → 解锁 → 交付结果、释放载荷。
 * 同一时刻由执行事实确定的判定和收尾当场完成；新增的检查或规划机会留到下一批。
 */

typedef struct HumanExecution {
    DominoThreadQueue inbox;
    thrd_t thread;
    uint32_t* todo_ring;  ///< 本地待办：人物下标 FIFO，每人至多一项。
    uint32_t todo_capacity;
    uint32_t todo_head;
    uint32_t todo_count;
    uint64_t batch_seq;
    DominoHumanExecutorStats stats;
} HumanExecution;

DominoHumanRuntime* domino_human_runtime_list;
static HumanExecution g_execution;

enum {
    HUMAN_WAKEUP_EVENT_TYPE = 1U,
};

/* ---------------------------------------------------------------------------------------------------------------- */
/* 待办与计时登记                                                                                                     */
/* ---------------------------------------------------------------------------------------------------------------- */

static void pushTodo(DominoHuman* human_ptr) {
    DominoHumanRuntime* runtime_ptr = dominoHumanRuntimeOf(human_ptr);
    if (runtime_ptr->todo_flag) {
        return;
    }
    runtime_ptr->todo_flag = true;
    g_execution.todo_ring[(g_execution.todo_head + g_execution.todo_count) % g_execution.todo_capacity] = dominoHumanIndexOf(human_ptr);
    g_execution.todo_count++;
}

static DominoHuman* popTodo(void) {
    const uint32_t human_index = g_execution.todo_ring[g_execution.todo_head];
    g_execution.todo_head = (g_execution.todo_head + 1U) % g_execution.todo_capacity;
    g_execution.todo_count--;
    domino_human_runtime_list[human_index].todo_flag = false;
    return &kv_A(domino_all_human_list, human_index);
}

/** @brief 先撤销当前唤醒身份，再标记取消；已出队的旧通知由消费端按身份忽略。 */
static void cancelWakeup(DominoHuman* human_ptr) {
    DominoHumanRuntime* runtime_ptr = dominoHumanRuntimeOf(human_ptr);
    const domino_game_timer_id_t game_timer_id = runtime_ptr->wakeup_game_timer_id;
    if (game_timer_id == 0U) {
        return;
    }
    runtime_ptr->wakeup_game_timer_id = 0U;
    runtime_ptr->wakeup_due_ms = 0U;
    const DOMINO_CODE code = dominoGameTimerCancel(game_timer_id, &g_execution.inbox);
    if (code != CODE_OK && code != ERR_NOT_FOUND) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot cancel human=%" PRIu32 " wakeup game_timer=%" PRIu64, human_ptr->human_id, game_timer_id);
    }
}

/** @brief 人物至多一个有效登记；已有登记不晚于新候选可以复用，否则取消旧登记再登记新时刻。 */
static void scheduleWakeup(DominoHuman* human_ptr, domino_runtime_ms_t due_ms) {
    DominoHumanRuntime* runtime_ptr = dominoHumanRuntimeOf(human_ptr);
    if (runtime_ptr->wakeup_game_timer_id != 0U && runtime_ptr->wakeup_due_ms <= due_ms) {
        return;
    }
    cancelWakeup(human_ptr);
    const DOMINO_CODE code =
        dominoGameTimerScheduleAt(&g_execution.inbox, due_ms, HUMAN_WAKEUP_EVENT_TYPE, human_ptr->human_id, &runtime_ptr->wakeup_game_timer_id);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot schedule human=%" PRIu32 " wakeup due_ms=%" PRIu32, human_ptr->human_id, due_ms);
    }
    runtime_ptr->wakeup_due_ms = due_ms;
}

/** @brief 只有身份与当前计时器匹配的通知才能清除登记；已取消通知跳过，未取消的旧通知仍可触发推进。 */
static DominoHuman* consumeWakeup(const DominoGameTimerRequestMessage* game_timer_ptr) {
    if (game_timer_ptr->cancelled_flag) {
        return nullptr;
    }
    DominoHuman* human_ptr = dominoGetHumanByID((domino_human_id_t)game_timer_ptr->data_id);
    if (!human_ptr) {
        return nullptr;
    }
    DominoHumanRuntime* runtime_ptr = dominoHumanRuntimeOf(human_ptr);
    if (runtime_ptr->wakeup_game_timer_id == game_timer_ptr->game_timer_id) {
        runtime_ptr->wakeup_game_timer_id = 0U;
        runtime_ptr->wakeup_due_ms = 0U;
    }
    return human_ptr;
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 下次规划时刻、行为规划重试与规划发起                                                                               */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 重试时刻未到返回 true；已到期时先消费旧时刻，不反复登记过去时刻。 */
static bool retryPending(uint8_t* flags_ptr, uint8_t valid_flag, domino_runtime_ms_t retry_at_ms, domino_runtime_ms_t now_ms) {
    if ((*flags_ptr & valid_flag) == 0U) {
        return false;
    }
    if (now_ms < retry_at_ms) {
        return true;
    }
    *flags_ptr = (uint8_t)(*flags_ptr & ~valid_flag);
    return false;
}

static domino_runtime_ms_t retryAt(domino_runtime_ms_t now_ms) {
    if (g_domino_human_ai_config.retry_delay_ms > UINT32_MAX - now_ms) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "plan retry time overflows now_ms=%" PRIu32, now_ms);
    }
    return now_ms + g_domino_human_ai_config.retry_delay_ms;
}

/** @brief 行为规划无方案、候选被拒或处理后任务仍需行为规划时，在任务上保留正延迟重试，避免在空结果与新请求之间忙循环。 */
static void setBehaviorRetry(const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms) {
    DominoTask* task_ptr = dominoGetTaskByID(slot_ptr->scope_id);
    if (task_ptr != nullptr) {
        task_ptr->behavior_retry_at_ms = retryAt(now_ms);
        task_ptr->flags |= DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID;
    }
}

/** @brief 覆盖动机或任务的下次规划时刻；valid_flag 为假表示“无”，清除该类时刻。 */
static void setNextPlan(DominoHumanRuntime* runtime_ptr, uint8_t planner_kind, bool valid_flag, domino_runtime_ms_t at_ms) {
    const uint8_t mask = (uint8_t)(1U << planner_kind);
    if (valid_flag) {
        runtime_ptr->next_plan_at_ms_list[planner_kind] = at_ms;
        runtime_ptr->next_plan_mask |= mask;
    } else {
        runtime_ptr->next_plan_at_ms_list[planner_kind] = 0U;
        runtime_ptr->next_plan_mask = (uint8_t)(runtime_ptr->next_plan_mask & ~mask);
    }
}

static uint8_t urgencyPriority(const DominoHuman* human_ptr) {
    return human_ptr->data->energy <= g_domino_human_ai_config.urgent_energy ? DOMINO_HUMAN_PLAN_PRIORITY_HIGH : DOMINO_HUMAN_PLAN_PRIORITY_NORMAL;
}

/** @brief 同一次写锁内：非阻塞入队取得请求 ID → 发布完整资格。请求不设期限，投递不改变人物唤醒。
 * @return 是否已投递；本批已投递过同类请求时留到下一批，返回 false。
 */
static bool postRequest(DominoHuman* human_ptr, uint8_t planner_kind, uint8_t priority, uint8_t scope_kind, uint32_t scope_id) {
    DominoHumanRuntime* runtime_ptr = dominoHumanRuntimeOf(human_ptr);
    if (runtime_ptr->posted_batch_seq_list[planner_kind] == g_execution.batch_seq) {
        /* 每人物每批每类至多新投递一次，留到下一批。 */
        pushTodo(human_ptr);
        return false;
    }
    const uint64_t request_id = dominoHumanPlannerPost(planner_kind, priority, human_ptr->human_id);
    runtime_ptr->slot_list[planner_kind] = (DominoHumanPlanSlot){
        .request_id = request_id,
        .scope_id = scope_id,
        .priority = priority,
        .scope_kind = scope_kind,
    };
    runtime_ptr->posted_batch_seq_list[planner_kind] = g_execution.batch_seq;
    g_execution.stats.posted_request_count_list[planner_kind]++;
    return true;
}

enum {
    BEHAVIOR_SCOPE_CANDIDATE_MAX = 2U,
};

/** @brief 行为规划只看运行任务和待执行队首，按询问次序写入 task_list 并返回个数。
 * @note 队首带让位请求时先看队首：回应任务首批步骤就绪，运行任务才能让位；否则先看运行任务，再为队首提前规划。
 * 队首之后的任务启动不了（普通推进不越过队首），轮到队首时再规划。
 */
static uint8_t behaviorScopeCandidates(const DominoHumanData* data_ptr, DominoTask* task_list[BEHAVIOR_SCOPE_CANDIDATE_MAX]) {
    DominoTask* active_ptr = data_ptr->active_task_id != 0U ? dominoGetTaskByID(data_ptr->active_task_id) : nullptr;
    DominoTask* head_ptr = data_ptr->task_queue_count != 0U ? dominoGetTaskByID(data_ptr->task_queue_list[0]) : nullptr;
    if (head_ptr != nullptr && (head_ptr->flags & DOMINO_TASK_FLAG_PREEMPT) != 0U) {
        DominoTask* swap_ptr = active_ptr;
        active_ptr = head_ptr;
        head_ptr = swap_ptr;
    }
    uint8_t count = 0U;
    if (active_ptr != nullptr) {
        task_list[count++] = active_ptr;
    }
    if (head_ptr != nullptr) {
        task_list[count++] = head_ptr;
    }
    return count;
}

/** @brief 规划发起只看廉价信号：动机和任务先把已到的下次规划时刻转为待规划标志，标志置位且请求槽空闲时以人物为范围投递，
 * 投递时清除标志；行为按运行任务和队首任务的计划缺口触发，并受任务上的重试时刻约束。
 * @note 执行器不复评动机、不判断动机是否需要新工作，人物没有有效动机本身也不发起规划。
 */
static void schedulePlanning(DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    DominoHumanRuntime* runtime_ptr = dominoHumanRuntimeOf(human_ptr);
    for (uint8_t kind = DOMINO_HUMAN_PLANNER_MOTIVATION; kind <= DOMINO_HUMAN_PLANNER_TASK; kind++) {
        const uint8_t mask = (uint8_t)(1U << kind);
        if ((runtime_ptr->next_plan_mask & mask) != 0U && runtime_ptr->next_plan_at_ms_list[kind] <= now_ms) {
            setNextPlan(runtime_ptr, kind, false, 0U);
            runtime_ptr->pending_mask |= mask;
        }
        if ((runtime_ptr->pending_mask & mask) != 0U && runtime_ptr->slot_list[kind].request_id == 0U &&
            postRequest(human_ptr, kind, urgencyPriority(human_ptr), DOMINO_HUMAN_PLAN_SCOPE_HUMAN, 0U)) {
            runtime_ptr->pending_mask = (uint8_t)(runtime_ptr->pending_mask & ~mask);
        }
    }
    if (runtime_ptr->slot_list[DOMINO_HUMAN_PLANNER_BEHAVIOR].request_id != 0U) {
        return;
    }
    DominoTask* task_list[BEHAVIOR_SCOPE_CANDIDATE_MAX];
    const uint8_t task_count = behaviorScopeCandidates(human_ptr->data, task_list);
    for (uint8_t index = 0U; index < task_count; index++) {
        DominoTask* task_ptr = task_list[index];
        if (retryPending(&task_ptr->flags, DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID, task_ptr->behavior_retry_at_ms, now_ms) ||
            !dominoHumanTaskNeedsBehaviorPlan(human_ptr, task_ptr)) {
            continue;
        }
        /* 已运行任务的修复及插入使用高优先级；高优先级不授予抢占运行任务的权限。 */
        const uint8_t priority = task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_OPEN ? DOMINO_HUMAN_PLAN_PRIORITY_HIGH : urgencyPriority(human_ptr);
        postRequest(human_ptr, DOMINO_HUMAN_PLANNER_BEHAVIOR, priority, DOMINO_HUMAN_PLAN_SCOPE_TASK, task_ptr->task_id);
        return;
    }
}

/** @brief 未来重试时刻参与唤醒汇总；已到期的旧时刻先消费，不反复登记当前或过去时刻。 */
static void mergeRetry(uint8_t* flags_ptr, uint8_t valid_flag, domino_runtime_ms_t retry_at_ms, domino_runtime_ms_t now_ms, uint64_t* due_ms_ptr) {
    if ((*flags_ptr & valid_flag) == 0U) {
        return;
    }
    if (retry_at_ms <= now_ms) {
        *flags_ptr = (uint8_t)(*flags_ptr & ~valid_flag);
        return;
    }
    if (retry_at_ms < *due_ms_ptr) {
        *due_ms_ptr = retry_at_ms;
    }
}

/** @brief 汇总动机和任务的下次规划时刻、运行任务和队首任务的行为规划重试时刻及预测检查时刻，人物只保留最早的一个有效登记。
 * @note 其他任务上的重试暂不参与；任务成为队首时由该次处理重新汇总。
 */
static void updateWakeup(DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    const DominoHumanRuntime* runtime_ptr = dominoHumanRuntimeOf(human_ptr);
    uint64_t due_ms = UINT64_MAX;
    for (uint8_t kind = DOMINO_HUMAN_PLANNER_MOTIVATION; kind <= DOMINO_HUMAN_PLANNER_TASK; kind++) {
        if ((runtime_ptr->next_plan_mask & (1U << kind)) != 0U && runtime_ptr->next_plan_at_ms_list[kind] < due_ms) {
            due_ms = runtime_ptr->next_plan_at_ms_list[kind];
        }
    }
    DominoTask* task_list[BEHAVIOR_SCOPE_CANDIDATE_MAX];
    const uint8_t task_count = behaviorScopeCandidates(human_ptr->data, task_list);
    for (uint8_t index = 0U; index < task_count; index++) {
        mergeRetry(&task_list[index]->flags, DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID, task_list[index]->behavior_retry_at_ms, now_ms, &due_ms);
    }
    domino_runtime_ms_t predicted_ms;
    if (dominoHumanPredictNextCheck(human_ptr, &predicted_ms) && predicted_ms < due_ms) {
        due_ms = predicted_ms;
    }
    if (due_ms == UINT64_MAX) {
        cancelWakeup(human_ptr);
    } else if (due_ms <= now_ms) {
        /* 已到的下次规划时刻在 schedulePlanning 中已转为标志；此处仅可能是同刻到期的保守检查。 */
        cancelWakeup(human_ptr);
        pushTodo(human_ptr);
    } else {
        scheduleWakeup(human_ptr, (domino_runtime_ms_t)due_ms);
    }
}

/** @brief 结算之后的运行期调度：回收 → 启动 → 状态 → 规划机会 → 唤醒。 */
static void finishHuman(DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    dominoHumanRecycleEnded(human_ptr);
    if (dominoHumanStartReadyWork(human_ptr, now_ms)) {
        /* 启动不改变本边界的判定；状态和唤醒基于启动后的事实。 */
    }
    human_ptr->state = dominoHumanComputeState(human_ptr);
    schedulePlanning(human_ptr, now_ms);
    updateWakeup(human_ptr, now_ms);
}

static void advanceHumanNow(DominoHuman* human_ptr) {
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    dominoHumanSettle(human_ptr, now_ms);
    finishHuman(human_ptr, now_ms);
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 候选审核与提交                                                                                                     */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 动机候选预检的累积上下文：已采用内容加当前项。 */
typedef struct MotivationReview {
    domino_motivation_id_t terminated_list[DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX];  ///< 本份候选已采用的终结目标。
    bool adopted_list[DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX];
    bool ignored_list[DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX];  ///< TERMINATE 目标已失效：忽略，不执行也不算拒绝。
    uint32_t new_type_mask;                                    ///< 本份候选已采用的新建类型。
    uint8_t new_count;
    uint8_t terminated_count;
} MotivationReview;

static bool terminatedEarlier(const MotivationReview* review_ptr, domino_motivation_id_t motivation_id) {
    for (uint8_t index = 0U; index < review_ptr->terminated_count; index++) {
        if (review_ptr->terminated_list[index] == motivation_id) {
            return true;
        }
    }
    return false;
}

/** @brief 复核动机条目：只核对结构性条件、容量和终结目标的有效性；动机的建立与终结本身是规划判断，不复核依据。 */
static bool checkMotivationEntry(const DominoHuman* human_ptr, const DominoMotivationCandidateEntry* entry_ptr, uint8_t index,
                                 MotivationReview* review_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    switch (entry_ptr->op) {
        case DOMINO_MOTIVATION_CANDIDATE_CREATE: {
            if (entry_ptr->type == DOMINO_MOTIVATION_TYPE_NONE || entry_ptr->type >= DOMINO_MOTIVATION_TYPE_COUNT) {
                return false;
            }
            /* 同类型已有有效动机（且未被本份候选更早终结），或本份候选已新建同类型：复用原动机，不新建。 */
            const DominoMotivation* active_ptr = dominoHumanFindActiveMotivation(data_ptr, entry_ptr->type);
            if ((active_ptr != nullptr && !terminatedEarlier(review_ptr, active_ptr->motivation_id)) ||
                (review_ptr->new_type_mask & (1U << entry_ptr->type)) != 0U) {
                return true;
            }
            if ((uint32_t)data_ptr->motivation_count + review_ptr->new_count >= DOMINO_HUMAN_MOTIVATION_COUNT_MAX ||
                dominoHumanStoreMotivationCount() + review_ptr->new_count >= DOMINO_HUMAN_MOTIVATION_POOL_MAX) {
                return false;
            }
            review_ptr->new_count++;
            review_ptr->new_type_mask |= 1U << entry_ptr->type;
            return true;
        }
        case DOMINO_MOTIVATION_CANDIDATE_TERMINATE: {
            if (entry_ptr->terminal_status != DOMINO_MOTIVATION_STATUS_ACHIEVED && entry_ptr->terminal_status != DOMINO_MOTIVATION_STATUS_FAILED &&
                entry_ptr->terminal_status != DOMINO_MOTIVATION_STATUS_REVOKED) {
                return false;
            }
            const DominoMotivation* motivation_ptr = dominoGetMotivationByID(entry_ptr->motivation_id);
            if (motivation_ptr == nullptr || motivation_ptr->owner_human_id != human_ptr->human_id ||
                motivation_ptr->status != DOMINO_MOTIVATION_STATUS_ACTIVE || terminatedEarlier(review_ptr, entry_ptr->motivation_id)) {
                /* 目标已回收、不属于该人物、已终结或本份候选已终结：忽略，重复终结没有副作用。 */
                review_ptr->ignored_list[index] = true;
                return true;
            }
            review_ptr->terminated_list[review_ptr->terminated_count++] = entry_ptr->motivation_id;
            return true;
        }
        default:
            return false;
    }
}

/** @brief 提交终结：写入终态；尚未启动的关联任务依法撤销（不制造执行记录），已启动的任务继续执行到自身结束，以后成功仍给原动机计次。
 * @note 撤销的任务与没有关联任务的已终结动机由随后的回收在同一次写锁内释放；动机自身来源的去重依据随动机回收丢弃。
 */
static void terminateMotivation(DominoHuman* human_ptr, domino_motivation_id_t motivation_id, uint8_t terminal_status, domino_runtime_ms_t now_ms) {
    dominoGetMotivationByID(motivation_id)->status = terminal_status;
    DominoHumanData* data_ptr = human_ptr->data;
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (task_ptr->motivation_id == motivation_id && task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_NONE && dominoTaskIsLive(task_ptr)) {
            const DominoTaskDecision decision = {.kind = DOMINO_TASK_DECISION_REVOKE, .reason = DOMINO_END_REASON_MOTIVATION_ENDED};
            dominoHumanApplyTaskDecision(human_ptr, task_ptr, decision, now_ms);
        }
    }
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                      "motivation terminated human=%" PRIu32 " motivation=%" PRIu32 " status=%u at_ms=%" PRIu32, human_ptr->human_id, motivation_id,
                      terminal_status, now_ms);
}

/** @brief 应用一个已采用的动机条目。
 * @return 是否实际新建或终结了动机。
 */
static bool applyMotivationEntry(DominoHuman* human_ptr, const DominoMotivationCandidateEntry* entry_ptr, bool ignored_flag,
                                 domino_runtime_ms_t now_ms) {
    if (entry_ptr->op == DOMINO_MOTIVATION_CANDIDATE_CREATE) {
        if (dominoHumanFindActiveMotivation(human_ptr->data, entry_ptr->type) != nullptr) {
            return false;
        }
        const DominoMotivation init = {.type = entry_ptr->type, .status = DOMINO_MOTIVATION_STATUS_ACTIVE};
        dominoHumanStoreAddMotivation(human_ptr->data, &init);
        return true;
    }
    if (ignored_flag) {
        return false;
    }
    terminateMotivation(human_ptr, entry_ptr->motivation_id, entry_ptr->terminal_status, now_ms);
    return true;
}

/** @brief 先检查不含可选项的必要部分，再按次序逐项尝试可选项；全部预检通过后提交，不再返回可恢复失败。
 * @note 实际新建或终结了动机时，同一次写锁内置任务待规划标志，让任务规划器看到新的动机集合。
 */
static bool commitMotivationCandidate(DominoHuman* human_ptr, const void* candidate_ptr, domino_runtime_ms_t now_ms) {
    if (!dominoHumanCandidateWithinLimits(candidate_ptr, DOMINO_HUMAN_PLANNER_MOTIVATION)) {
        return false;
    }
    const DominoPlanCandidateHeader* header_ptr = dominoHumanCandidateHeader(candidate_ptr);
    const DominoMotivationCandidateEntry* entry_list = dominoHumanCandidateEntries((void*)candidate_ptr);
    MotivationReview review = {0};
    for (uint8_t index = 0U; index < header_ptr->entry_count; index++) {
        review.adopted_list[index] = checkMotivationEntry(human_ptr, &entry_list[index], index, &review);
        if (!review.adopted_list[index] && index < header_ptr->required_count) {
            return false;
        }
    }
    bool changed_flag = false;
    for (uint8_t index = 0U; index < header_ptr->entry_count; index++) {
        if (review.adopted_list[index]) {
            changed_flag = applyMotivationEntry(human_ptr, &entry_list[index], review.ignored_list[index], now_ms) || changed_flag;
        }
    }
    if (changed_flag) {
        dominoHumanMarkPlanPending(human_ptr, DOMINO_HUMAN_PLAN_MASK_TASK);
    }
    return true;
}

/** @brief 任务候选预检的累积上下文：已采用内容加当前项。 */
typedef struct TaskReviewContext {
    bool adopted_list[DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX];
    bool reuse_list[DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX];  ///< 同次工作已有任务，复用原 task_id。
    domino_task_id_t resolved_id_list[DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX];
    uint8_t new_task_count;
    bool stimulus_taken_flag;  ///< 本份候选已有新任务承接待处理外部信息。
} TaskReviewContext;

/** @brief 已接受任务中是否存在同一工作身份；同一人物的同一次工作只承接一次。 */
static domino_task_id_t findSameWork(const DominoHumanData* data_ptr, const DominoTaskCandidateEntry* entry_ptr) {
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        const DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (dominoTaskIsLive(task_ptr) && task_ptr->type == entry_ptr->type && task_ptr->work_source_kind == entry_ptr->work_source_kind &&
            task_ptr->work_source_id == entry_ptr->work_source_id && task_ptr->work_round == entry_ptr->work_round) {
            return task_ptr->task_id;
        }
    }
    return 0U;
}

/** @brief 核对新任务的业务来源：动机来源须为所属动机本身；外部信息来源须对应人物当前尚未承接的外部信息（来源和轮次一致），
 * 且同份候选只承接一次。回应任务挂在哪个有效动机下、运行任务的应激选择都是规划判断，不复核。
 */
static bool checkTaskSource(const DominoHuman* human_ptr, const DominoTaskCandidateEntry* entry_ptr, const DominoMotivation* motivation_ptr,
                            const TaskReviewContext* context_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    switch (entry_ptr->work_source_kind) {
        case DOMINO_TASK_SOURCE_MOTIVATION:
            return entry_ptr->work_source_id == motivation_ptr->motivation_id;
        case DOMINO_TASK_SOURCE_STIMULUS:
            return !context_ptr->stimulus_taken_flag && data_ptr->stimulus_kind != DOMINO_HUMAN_STIMULUS_NONE &&
                   entry_ptr->work_source_id == data_ptr->stimulus_source_id && entry_ptr->work_round == data_ptr->stimulus_round;
        default:
            return false;
    }
}

static bool checkTaskCreate(const DominoHuman* human_ptr, const DominoTaskCandidateEntry* entry_list, uint8_t index, TaskReviewContext* context_ptr) {
    const DominoTaskCandidateEntry* entry_ptr = &entry_list[index];
    const DominoHumanData* data_ptr = human_ptr->data;
    if (dominoHumanGetTaskRules(entry_ptr->type) == nullptr || entry_ptr->target_value > DOMINO_HUMAN_NEED_MAX) {
        return false;
    }
    const DominoMotivation* motivation_ptr = dominoGetMotivationByID(entry_ptr->motivation_id);
    if (motivation_ptr == nullptr || motivation_ptr->owner_human_id != human_ptr->human_id ||
        motivation_ptr->status != DOMINO_MOTIVATION_STATUS_ACTIVE) {
        return false;
    }
    /* 同次工作已有任务：复用原 task_id，不新建、不改变原归属。 */
    const domino_task_id_t same_id = findSameWork(data_ptr, entry_ptr);
    if (same_id != 0U) {
        context_ptr->reuse_list[index] = true;
        context_ptr->resolved_id_list[index] = same_id;
        return true;
    }
    for (uint8_t earlier = 0U; earlier < index; earlier++) {
        const DominoTaskCandidateEntry* other_ptr = &entry_list[earlier];
        if (context_ptr->adopted_list[earlier] && other_ptr->op == DOMINO_TASK_CANDIDATE_CREATE && other_ptr->type == entry_ptr->type &&
            other_ptr->work_source_kind == entry_ptr->work_source_kind && other_ptr->work_source_id == entry_ptr->work_source_id &&
            other_ptr->work_round == entry_ptr->work_round) {
            context_ptr->reuse_list[index] = true;
            return true;
        }
    }
    if (!checkTaskSource(human_ptr, entry_ptr, motivation_ptr, context_ptr)) {
        return false;
    }
    if (entry_ptr->type == DOMINO_TASK_TYPE_SLEEP) {
        /* 睡眠需求轮次是身份锚点：来源为所属恢复精力动机，读取时轮次等于当前轮次、工作轮次为其下一轮，上一轮已结束且轮次未耗尽。
         * 现在是否需要睡觉是任务规划器的判断，不复核精力。 */
        if (entry_ptr->work_source_kind != DOMINO_TASK_SOURCE_MOTIVATION || motivation_ptr->type != DOMINO_MOTIVATION_TYPE_RESTORE_ENERGY ||
            entry_ptr->expected_motivation_round != motivation_ptr->need_round || motivation_ptr->need_round == UINT32_MAX ||
            entry_ptr->work_round != motivation_ptr->need_round + 1U || motivation_ptr->round_task_id != 0U) {
            return false;
        }
    }
    const uint32_t queue_count = (uint32_t)data_ptr->task_queue_count + context_ptr->new_task_count;
    if ((uint32_t)data_ptr->task_count + context_ptr->new_task_count >= DOMINO_HUMAN_TASK_COUNT_MAX ||
        dominoHumanStoreTaskCount() + context_ptr->new_task_count >= DOMINO_HUMAN_TASK_POOL_MAX || queue_count >= DOMINO_HUMAN_TASK_COUNT_MAX) {
        return false;
    }
    if (entry_ptr->work_source_kind != DOMINO_TASK_SOURCE_STIMULUS && entry_ptr->insert_index != DOMINO_PLAN_CANDIDATE_INSERT_APPEND &&
        (entry_ptr->insert_index > data_ptr->task_queue_count ||
         entry_ptr->expected_queue_version != dominoHumanRuntimeOf(human_ptr)->queue_version)) {
        return false;
    }
    context_ptr->new_task_count++;
    context_ptr->stimulus_taken_flag = context_ptr->stimulus_taken_flag || entry_ptr->work_source_kind == DOMINO_TASK_SOURCE_STIMULUS;
    return true;
}

static bool checkTaskEntry(const DominoHuman* human_ptr, const DominoTaskCandidateEntry* entry_list, uint8_t index, TaskReviewContext* context_ptr) {
    const DominoTaskCandidateEntry* entry_ptr = &entry_list[index];
    switch (entry_ptr->op) {
        case DOMINO_TASK_CANDIDATE_CREATE:
            return checkTaskCreate(human_ptr, entry_list, index, context_ptr);
        case DOMINO_TASK_CANDIDATE_CANCEL: {
            const DominoTask* task_ptr = dominoGetTaskByID(entry_ptr->task_id);
            if (task_ptr == nullptr || task_ptr->owner_human_id != human_ptr->human_id || !dominoTaskIsLive(task_ptr)) {
                return false;
            }
            for (uint8_t earlier = 0U; earlier < index; earlier++) {
                if (context_ptr->adopted_list[earlier] && entry_list[earlier].op == DOMINO_TASK_CANDIDATE_CANCEL &&
                    entry_list[earlier].task_id == entry_ptr->task_id) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

/** @brief 建立任务记录并进入待执行队列：回应外部信息的任务放在队首并带让位请求，同次写入中承接外部信息。 */
static void applyTaskCreate(DominoHuman* human_ptr, const DominoTaskCandidateEntry* entry_ptr, TaskReviewContext* context_ptr, uint8_t index,
                            domino_runtime_ms_t now_ms) {
    DominoHumanData* data_ptr = human_ptr->data;
    const bool stimulus_flag = entry_ptr->work_source_kind == DOMINO_TASK_SOURCE_STIMULUS;
    DominoTask init = {
        .motivation_id = entry_ptr->motivation_id,
        .work_source_id = entry_ptr->work_source_id,
        .work_round = entry_ptr->work_round,
        .accepted_at_ms = now_ms,
        .deadline_ms = entry_ptr->deadline_ms,
        .type = entry_ptr->type,
        .stimulus_kind = stimulus_flag ? data_ptr->stimulus_kind : (uint8_t)DOMINO_HUMAN_STIMULUS_NONE,
        .work_source_kind = entry_ptr->work_source_kind,
        .target_value = entry_ptr->target_value,
        .flags = (uint8_t)((entry_ptr->task_flags & (DOMINO_TASK_FLAG_DEADLINE_VALID | DOMINO_TASK_FLAG_DEADLINE_INCLUSIVE)) |
                           (stimulus_flag ? DOMINO_TASK_FLAG_PREEMPT : 0U)),
    };
    const domino_task_id_t task_id = dominoHumanStoreAddTask(data_ptr, &init)->task_id;
    uint8_t insert_index = entry_ptr->insert_index == DOMINO_PLAN_CANDIDATE_INSERT_APPEND ? data_ptr->task_queue_count : entry_ptr->insert_index;
    if (stimulus_flag) {
        insert_index = 0U;
        data_ptr->stimulus_kind = DOMINO_HUMAN_STIMULUS_NONE;
    }
    dominoHumanQueueInsert(data_ptr, insert_index, task_id);
    dominoHumanRuntimeOf(human_ptr)->queue_version++;
    if (entry_ptr->type == DOMINO_TASK_TYPE_SLEEP) {
        DominoMotivation* motivation_ptr = dominoGetMotivationByID(entry_ptr->motivation_id);
        motivation_ptr->need_round = entry_ptr->work_round;
        motivation_ptr->round_task_id = task_id;
    }
    context_ptr->resolved_id_list[index] = task_id;
}

static bool commitTaskCandidate(DominoHuman* human_ptr, const void* candidate_ptr, domino_runtime_ms_t now_ms) {
    if (!dominoHumanCandidateWithinLimits(candidate_ptr, DOMINO_HUMAN_PLANNER_TASK)) {
        return false;
    }
    const DominoPlanCandidateHeader* header_ptr = dominoHumanCandidateHeader(candidate_ptr);
    const DominoTaskCandidateEntry* entry_list = dominoHumanCandidateEntries((void*)candidate_ptr);
    TaskReviewContext context = {0};
    for (uint8_t index = 0U; index < header_ptr->entry_count; index++) {
        context.adopted_list[index] = checkTaskEntry(human_ptr, entry_list, index, &context);
        if (!context.adopted_list[index] && index < header_ptr->required_count) {
            return false;
        }
    }
    for (uint8_t index = 0U; index < header_ptr->entry_count; index++) {
        if (!context.adopted_list[index]) {
            continue;
        }
        const DominoTaskCandidateEntry* entry_ptr = &entry_list[index];
        if (entry_ptr->op == DOMINO_TASK_CANDIDATE_CREATE) {
            if (context.reuse_list[index]) {
                if (context.resolved_id_list[index] == 0U) {
                    /* 同份候选内的重复建议复用更早条目的正式 ID。 */
                    for (uint8_t earlier = 0U; earlier < index; earlier++) {
                        if (context.adopted_list[earlier] && entry_list[earlier].op == DOMINO_TASK_CANDIDATE_CREATE &&
                            entry_list[earlier].type == entry_ptr->type && entry_list[earlier].work_source_kind == entry_ptr->work_source_kind &&
                            entry_list[earlier].work_source_id == entry_ptr->work_source_id &&
                            entry_list[earlier].work_round == entry_ptr->work_round) {
                            context.resolved_id_list[index] = context.resolved_id_list[earlier];
                            break;
                        }
                    }
                }
                continue;
            }
            applyTaskCreate(human_ptr, entry_ptr, &context, index, now_ms);
            continue;
        }
        DominoTask* task_ptr = dominoGetTaskByID(entry_ptr->task_id);
        const DominoTaskDecision decision = {
            .kind = task_ptr->exec_status == DOMINO_TASK_EXEC_STATUS_NONE ? DOMINO_TASK_DECISION_REVOKE : DOMINO_TASK_DECISION_CANCEL,
            .reason = DOMINO_END_REASON_PLANNER_CANCELLED,
        };
        dominoHumanApplyTaskDecision(human_ptr, task_ptr, decision, now_ms);
    }
    return true;
}

/** @brief 行为候选一个条目的预检结论，供提交阶段复用。 */
typedef struct BehaviorReview {
    bool adopted_list[DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX];
    int64_t plan_delta;      ///< 已采用条目引起的全局计划项变化。
    bool insert_taken_flag;  ///< 本份候选已有插入条目承接待处理外部信息。
} BehaviorReview;

/** @brief 本任务当前行为中步骤序号为 step_seq 的记录；没有返回空指针。 */
static const DominoBehavior* currentStepOf(const DominoTask* task_ptr, uint32_t step_seq) {
    for (uint8_t channel = 0U; channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
        const domino_behavior_id_t behavior_id = task_ptr->current_behavior_id_list[channel];
        if (behavior_id != 0U && dominoGetBehaviorByID(behavior_id)->step_seq == step_seq) {
            return dominoGetBehaviorByID(behavior_id);
        }
    }
    return nullptr;
}

/** @brief 核对 REPLACE 步骤：类型参数、通道、标志和依赖。依赖只能指向本条目更早的步骤或本任务当前行为，因此天然无环；
 * 伴随依赖要求来源在另一通道；任何步骤都不能依赖插入的辅助行为。
 */
static bool checkReplaceSteps(const DominoTask* task_ptr, const DominoBehaviorPlanItem* step_list, uint16_t step_count) {
    for (uint16_t step = 0U; step < step_count; step++) {
        const DominoBehaviorPlanItem* item_ptr = &step_list[step];
        const DominoBehaviorRules* rules_ptr = dominoHumanGetBehaviorRules(item_ptr->type);
        if (rules_ptr == nullptr || !rules_ptr->validate_step(item_ptr) || item_ptr->channel_index >= DOMINO_BEHAVIOR_CHANNEL_COUNT ||
            item_ptr->flags != 0U) {
            return false;
        }
        for (uint8_t dep = 0U; dep < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; dep++) {
            const DominoBehaviorDependency* dependency_ptr = &item_ptr->dependency_list[dep];
            if (dependency_ptr->kind == DOMINO_BEHAVIOR_DEPENDENCY_NONE) {
                if (dependency_ptr->step_seq != 0U) {
                    return false;
                }
                continue;
            }
            if (dependency_ptr->kind >= DOMINO_BEHAVIOR_DEPENDENCY_KIND_COUNT || dependency_ptr->state != DOMINO_BEHAVIOR_DEPENDENCY_PENDING) {
                return false;
            }
            uint8_t source_channel;
            if ((dependency_ptr->step_seq & DOMINO_PLAN_CANDIDATE_LOCAL_REF) != 0U) {
                const uint32_t local = dependency_ptr->step_seq & ~DOMINO_PLAN_CANDIDATE_LOCAL_REF;
                if (local >= step) {
                    return false;
                }
                source_channel = step_list[local].channel_index;
            } else {
                const DominoBehavior* source_ptr = currentStepOf(task_ptr, dependency_ptr->step_seq);
                if (source_ptr == nullptr || (source_ptr->flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U) {
                    return false;
                }
                source_channel = source_ptr->channel_index;
            }
            if (dependency_ptr->kind == DOMINO_BEHAVIOR_DEPENDENCY_WHILE && source_channel == item_ptr->channel_index) {
                return false;
            }
        }
    }
    return true;
}

/** @brief 核对 INSERT 步骤：只用辅助通道，没有依赖，类型参数合法。 */
static bool checkInsertSteps(const DominoBehaviorPlanItem* step_list, uint16_t step_count) {
    for (uint16_t step = 0U; step < step_count; step++) {
        const DominoBehaviorPlanItem* item_ptr = &step_list[step];
        const DominoBehaviorRules* rules_ptr = dominoHumanGetBehaviorRules(item_ptr->type);
        if (rules_ptr == nullptr || !rules_ptr->validate_step(item_ptr) || item_ptr->channel_index == DOMINO_BEHAVIOR_CHANNEL_MAIN ||
            item_ptr->channel_index >= DOMINO_BEHAVIOR_CHANNEL_COUNT) {
            return false;
        }
        for (uint8_t dep = 0U; dep < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; dep++) {
            if (item_ptr->dependency_list[dep].kind != DOMINO_BEHAVIOR_DEPENDENCY_NONE || item_ptr->dependency_list[dep].step_seq != 0U) {
                return false;
            }
        }
    }
    return true;
}

/** @brief 本任务未来步骤中插入的辅助步骤数。 */
static uint8_t insertedPlanCount(const DominoTask* task_ptr) {
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < task_ptr->plan_count; index++) {
        count = (uint8_t)(count + ((task_ptr->plan_item_list[index].flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U ? 1U : 0U));
    }
    return count;
}

static bool checkBehaviorEntry(const DominoHuman* human_ptr, const DominoBehaviorCandidateEntry* entry_list, uint8_t index,
                               const DominoPlanCandidateHeader* header_ptr, const DominoBehaviorPlanItem* step_list, BehaviorReview* review_ptr,
                               int64_t* entry_delta_ptr) {
    const DominoBehaviorCandidateEntry* entry_ptr = &entry_list[index];
    const DominoTask* task_ptr = dominoGetTaskByID(entry_ptr->task_id);
    if (task_ptr == nullptr || task_ptr->owner_human_id != human_ptr->human_id || !dominoTaskIsLive(task_ptr) ||
        (uint32_t)entry_ptr->step_index + entry_ptr->step_count > header_ptr->step_count) {
        return false;
    }
    for (uint8_t earlier = 0U; earlier < index; earlier++) {
        /* 同一任务每份候选至多调整一次，容量按调整前的计划核对。 */
        if (review_ptr->adopted_list[earlier] && entry_list[earlier].task_id == entry_ptr->task_id) {
            return false;
        }
    }
    const DominoBehaviorPlanItem* own_list = &step_list[entry_ptr->step_index];
    uint32_t kept_count;
    switch (entry_ptr->op) {
        case DOMINO_BEHAVIOR_CANDIDATE_REPLACE:
            if (task_ptr->plan_version != entry_ptr->expected_plan_version || (task_ptr->flags & DOMINO_TASK_FLAG_COMPLETION_REACHED) != 0U) {
                return false;
            }
            for (uint8_t channel = 0U; entry_ptr->depends_current_flag != 0U && channel < DOMINO_BEHAVIOR_CHANNEL_COUNT; channel++) {
                if (task_ptr->current_behavior_id_list[channel] != entry_ptr->expected_current_behavior_id_list[channel]) {
                    return false;
                }
            }
            if (!checkReplaceSteps(task_ptr, own_list, entry_ptr->step_count)) {
                return false;
            }
            kept_count = insertedPlanCount(task_ptr);
            break;
        case DOMINO_BEHAVIOR_CANDIDATE_INSERT:
            /* 只能为运行任务插入；外部信息须尚未承接且轮次未变，这是插入回应哪条外部信息的身份锚点，也让回应任务与插入互斥。
             * 运行任务是否接受插入是规划判断，不复核应激选择。 */
            if (review_ptr->insert_taken_flag || task_ptr->task_id != human_ptr->data->active_task_id || entry_ptr->step_count == 0U ||
                human_ptr->data->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE ||
                entry_ptr->expected_stimulus_round != human_ptr->data->stimulus_round || !checkInsertSteps(own_list, entry_ptr->step_count)) {
                return false;
            }
            kept_count = task_ptr->plan_count;
            break;
        default:
            return false;
    }
    const uint32_t new_plan_count = kept_count + entry_ptr->step_count;
    if (new_plan_count + task_ptr->record_count > DOMINO_TASK_STEP_COUNT_MAX) {
        return false;
    }
    const int64_t entry_delta = (int64_t)new_plan_count - task_ptr->plan_count;
    if ((int64_t)dominoHumanStorePlanItemCount() + review_ptr->plan_delta + entry_delta > (int64_t)DOMINO_HUMAN_PLAN_ITEM_POOL_MAX) {
        return false;
    }
    *entry_delta_ptr = entry_delta;
    return true;
}

/** @brief 为新步骤分配本任务内的步骤序号，并把条目内局部依赖解析为正式序号；依赖状态从 PENDING 开始。序号耗尽致命退出。 */
static void assignSteps(DominoTask* task_ptr, const DominoBehaviorPlanItem* step_list, uint16_t step_count, uint8_t flags,
                        DominoBehaviorPlanItem* out_list) {
    const uint32_t base_seq = task_ptr->last_step_seq;
    if (step_count > DOMINO_BEHAVIOR_STEP_SEQ_MAX - base_seq) {
        DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "task=%u behavior step sequence exhausted", task_ptr->task_id);
    }
    for (uint16_t step = 0U; step < step_count; step++) {
        DominoBehaviorPlanItem item = step_list[step];
        item.step_seq = base_seq + step + 1U;
        item.flags = flags;
        for (uint8_t dep = 0U; dep < DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX; dep++) {
            DominoBehaviorDependency* dependency_ptr = &item.dependency_list[dep];
            if ((dependency_ptr->step_seq & DOMINO_PLAN_CANDIDATE_LOCAL_REF) != 0U) {
                dependency_ptr->step_seq = base_seq + (dependency_ptr->step_seq & ~DOMINO_PLAN_CANDIDATE_LOCAL_REF) + 1U;
            }
            dependency_ptr->state = DOMINO_BEHAVIOR_DEPENDENCY_PENDING;
        }
        out_list[step] = item;
    }
    task_ptr->last_step_seq = base_seq + step_count;
}

/** @brief REPLACE：保留已插入的辅助步骤在前（各自通道的先后不变），其后为新的自身步骤；INSERT：新插入步骤排在各自通道最前。 */
static void applyBehaviorEntry(DominoHuman* human_ptr, const DominoBehaviorCandidateEntry* entry_ptr, const DominoBehaviorPlanItem* step_list) {
    DominoTask* task_ptr = dominoGetTaskByID(entry_ptr->task_id);
    DominoBehaviorPlanItem plan_list[DOMINO_TASK_STEP_COUNT_MAX];
    uint8_t plan_count = 0U;
    const DominoBehaviorPlanItem* own_list = &step_list[entry_ptr->step_index];
    if (entry_ptr->op == DOMINO_BEHAVIOR_CANDIDATE_REPLACE) {
        for (uint8_t index = 0U; index < task_ptr->plan_count; index++) {
            if ((task_ptr->plan_item_list[index].flags & DOMINO_BEHAVIOR_FLAG_INSERTED) != 0U) {
                plan_list[plan_count++] = task_ptr->plan_item_list[index];
            }
        }
        assignSteps(task_ptr, own_list, entry_ptr->step_count, 0U, &plan_list[plan_count]);
        plan_count = (uint8_t)(plan_count + entry_ptr->step_count);
        task_ptr->flags = (uint8_t)(task_ptr->flags & ~DOMINO_TASK_FLAG_BEHAVIOR_RETRY_VALID);
    } else {
        assignSteps(task_ptr, own_list, entry_ptr->step_count, DOMINO_BEHAVIOR_FLAG_INSERTED, plan_list);
        plan_count = (uint8_t)entry_ptr->step_count;
        for (uint8_t index = 0U; index < task_ptr->plan_count; index++) {
            plan_list[plan_count++] = task_ptr->plan_item_list[index];
        }
        /* 插入步骤承接待处理外部信息。 */
        human_ptr->data->stimulus_kind = DOMINO_HUMAN_STIMULUS_NONE;
    }
    /* 每个任务的未来步骤整体替换；正式记录复制值，不借用候选内存。 */
    dominoHumanStoreReplacePlan(task_ptr, plan_list, plan_count);
}

static bool commitBehaviorCandidate(DominoHuman* human_ptr, const void* candidate_ptr) {
    if (!dominoHumanCandidateWithinLimits(candidate_ptr, DOMINO_HUMAN_PLANNER_BEHAVIOR)) {
        return false;
    }
    const DominoPlanCandidateHeader* header_ptr = dominoHumanCandidateHeader(candidate_ptr);
    const DominoBehaviorCandidateEntry* entry_list = dominoHumanCandidateEntries((void*)candidate_ptr);
    const DominoBehaviorPlanItem* step_list = dominoHumanCandidateSteps((void*)candidate_ptr);
    BehaviorReview review = {0};
    for (uint8_t index = 0U; index < header_ptr->entry_count; index++) {
        int64_t entry_delta = 0;
        const bool ok_flag = checkBehaviorEntry(human_ptr, entry_list, index, header_ptr, step_list, &review, &entry_delta);
        if (!ok_flag && index < header_ptr->required_count) {
            return false;
        }
        review.adopted_list[index] = ok_flag;
        if (ok_flag) {
            review.plan_delta += entry_delta;
            review.insert_taken_flag = review.insert_taken_flag || entry_list[index].op == DOMINO_BEHAVIOR_CANDIDATE_INSERT;
        }
    }
    for (uint8_t index = 0U; index < header_ptr->entry_count; index++) {
        if (review.adopted_list[index]) {
            applyBehaviorEntry(human_ptr, &entry_list[index], step_list);
        }
    }
    return true;
}

/** @brief 处理后该任务是否仍满足行为规划触发条件；任务已不存在时为否。 */
static bool behaviorScopeStillNeeded(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr) {
    const DominoTask* task_ptr = dominoGetTaskByID(slot_ptr->scope_id);
    return task_ptr != nullptr && task_ptr->owner_human_id == human_ptr->human_id && dominoHumanTaskNeedsBehaviorPlan(human_ptr, task_ptr);
}

/** @brief 核对完整请求身份 → 共同结算 → 复核并提交或拒绝 → 动机和任务覆盖下次规划时刻、行为按结果设重试 → 边界判定 → 后续调度；
 * 载荷整块释放一次。
 * @note 只保留请求身份、结构性条件和身份锚点三类复核（宪法第六章），不复核判断依据、不按时间拒收结果；结果不改变待规划标志，
 * 请求在途期间新置位的标志在处理后再发起一次请求。
 */
static void processResult(uint8_t planner_kind, uint8_t priority, DominoHumanPlanResultMessage* result_ptr) {
    g_execution.stats.result_count_list[planner_kind][priority]++;
    const uint64_t wait_ns = dominoMonotonicTimeNs() - result_ptr->enqueued_mono_ns;
    if (wait_ns > g_execution.stats.max_result_wait_ns) {
        g_execution.stats.max_result_wait_ns = wait_ns;
    }
    DominoHuman* human_ptr = dominoGetHumanByID(result_ptr->human_id);
    DominoHumanPlanSlot* slot_ptr = human_ptr ? &dominoHumanRuntimeOf(human_ptr)->slot_list[planner_kind] : nullptr;
    if (!slot_ptr || slot_ptr->request_id != result_ptr->request_id || slot_ptr->priority != priority) {
        /* 旧或重复结果只释放自身载荷，不清除另一优先级或后续请求，也不改变下次规划时刻。 */
        g_execution.stats.stale_result_count++;
        dominoHumanCandidateFree(result_ptr->candidate_ptr);
        return;
    }
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    const DominoHumanPlanSlot matched = *slot_ptr;
    /* 结算到本次时刻是写入的前提，不是时效性检查。 */
    dominoHumanSettle(human_ptr, now_ms);
    bool rejected_flag = false;
    if (result_ptr->outcome == DOMINO_HUMAN_PLAN_OUTCOME_CANDIDATE) {
        bool accepted_flag = false;
        switch (planner_kind) {
            case DOMINO_HUMAN_PLANNER_MOTIVATION:
                accepted_flag = commitMotivationCandidate(human_ptr, result_ptr->candidate_ptr, now_ms);
                break;
            case DOMINO_HUMAN_PLANNER_TASK:
                accepted_flag = commitTaskCandidate(human_ptr, result_ptr->candidate_ptr, now_ms);
                break;
            default:
                accepted_flag = commitBehaviorCandidate(human_ptr, result_ptr->candidate_ptr);
                break;
        }
        if (accepted_flag) {
            g_execution.stats.accepted_candidate_count++;
        } else {
            g_execution.stats.rejected_candidate_count++;
        }
        rejected_flag = !accepted_flag;
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "plan human=%" PRIu32 " kind=%u request=%" PRIu64 " %s",
                          human_ptr->human_id, planner_kind, matched.request_id, accepted_flag ? "accepted" : "rejected");
    }
    DominoHumanRuntime* runtime_ptr = dominoHumanRuntimeOf(human_ptr);
    if (planner_kind == DOMINO_HUMAN_PLANNER_BEHAVIOR) {
        /* 行为仍按扫描触发：无方案、被拒或处理后任务仍需行为规划时保留正延迟重试，避免在空结果与新请求之间忙循环。 */
        if (result_ptr->outcome == DOMINO_HUMAN_PLAN_OUTCOME_NO_PLAN || rejected_flag || behaviorScopeStillNeeded(human_ptr, &matched)) {
            setBehaviorRetry(&matched, now_ms);
        }
    } else if (rejected_flag) {
        /* 整份候选被复核拒绝：规划器给出的时刻以候选被接受为前提，导致拒绝的变化也不一定置标志，因此设兜底时刻；
         * 超出 32 位业务毫秒时视为“无”。只有可选条目被省略时不算整份拒绝。 */
        const bool valid_flag = g_domino_human_ai_config.retry_delay_ms <= UINT32_MAX - now_ms;
        setNextPlan(runtime_ptr, planner_kind, valid_flag, valid_flag ? now_ms + g_domino_human_ai_config.retry_delay_ms : 0U);
    } else {
        /* 包括 NO_CHANGE 和 NO_PLAN 在内，都用规划器给出的下次规划时刻覆盖当前值；“无”时清除。 */
        setNextPlan(runtime_ptr, planner_kind, result_ptr->next_plan_valid_flag, result_ptr->next_plan_at_ms);
    }
    /* 消费本次请求资格。 */
    *slot_ptr = (DominoHumanPlanSlot){0};
    dominoHumanCandidateFree(result_ptr->candidate_ptr);
    result_ptr->candidate_ptr = nullptr;
    /* 候选提交引起的变化先在本边界检查完，再让位、启动或恢复行为。 */
    dominoHumanResolveBoundary(human_ptr);
    finishHuman(human_ptr, now_ms);
}

#ifdef DOMINO_PLAYGROUND_MODE
/* ---------------------------------------------------------------------------------------------------------------- */
/* 测试验收专用：人物输入（清理测试代码时一并删除）                                                                    */
/* ---------------------------------------------------------------------------------------------------------------- */

/** @brief 测试验收专用：睡眠条件只修改标志，失效后的处置由随后的统一决策完成。
 * @return 是否实际变化。
 */
static bool applyTestSleepCondition(DominoHumanData* data_ptr, bool forbidden_flag) {
    if (((data_ptr->ai_flags & DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN) != 0U) == forbidden_flag) {
        return false;
    }
    if (forbidden_flag) {
        data_ptr->ai_flags |= DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN;
    } else {
        data_ptr->ai_flags = (uint8_t)(data_ptr->ai_flags & ~DOMINO_HUMAN_AI_FLAG_SLEEP_FORBIDDEN);
    }
    return true;
}

/** @brief 测试验收专用：写入新精力后交给仍承担责任的任务记录输入事实。
 * @return 是否实际变化。
 */
static bool applyTestEnergy(DominoHumanData* data_ptr, uint8_t energy) {
    if (data_ptr->energy == energy) {
        return false;
    }
    data_ptr->energy = energy;
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (dominoTaskIsLive(task_ptr)) {
            dominoHumanGetTaskRules(task_ptr->type)->on_energy_input(task_ptr, energy);
        }
    }
    return true;
}

/** @brief 测试验收专用：来源与类别同次写入；类别由 NONE 变为非 NONE 时开启新一轮外部信息，回应由运行任务的应激规则
 * 在随后的统一调度中选择。
 * @note 最小实现只保存一条待处理外部信息；同类信息未被承接时再次到达不重复开轮，只更新来源。
 * @return 是否实际变化。
 */
static bool applyTestStimulus(DominoHuman* human_ptr, const DominoHumanTestStimulusInput* input_ptr) {
    DominoHumanData* data_ptr = human_ptr->data;
    if (data_ptr->stimulus_source_id == input_ptr->source_id && data_ptr->stimulus_kind == input_ptr->stimulus_kind) {
        return false;
    }
    data_ptr->stimulus_source_id = input_ptr->source_id;
    if (input_ptr->stimulus_kind != DOMINO_HUMAN_STIMULUS_NONE && data_ptr->stimulus_kind == DOMINO_HUMAN_STIMULUS_NONE) {
        if (data_ptr->stimulus_round == UINT32_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "human=%" PRIu32 " stimulus round exhausted", human_ptr->human_id);
        }
        data_ptr->stimulus_round++;
    }
    data_ptr->stimulus_kind = input_ptr->stimulus_kind;
    return true;
}

/** @brief 测试验收专用：人物输入先按旧值结算到本次时刻，再写入实际变化、置相应待规划标志，并在同一边界判定和收尾；人物不存在时忽略。
 * @pre 执行器持写锁，或 playground 串行直访。
 */
static void processTestInput(const DominoHumanExecutionMessage* message_ptr) {
    domino_human_id_t human_id;
    switch (message_ptr->type) {
        case DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_SLEEP_CONDITION:
            human_id = message_ptr->payload.test_sleep_condition.human_id;
            break;
        case DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_ENERGY:
            human_id = message_ptr->payload.test_energy.human_id;
            break;
        case DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_STIMULUS:
            human_id = message_ptr->payload.test_stimulus.human_id;
            break;
        default:
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_MESSAGE, "invalid human test input type=%d", (int)message_ptr->type);
    }
    DominoHuman* human_ptr = dominoGetHumanByID(human_id);
    if (!human_ptr) {
        return;
    }
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    dominoHumanSettle(human_ptr, now_ms);
    bool changed_flag;
    uint8_t plan_mask;
    switch (message_ptr->type) {
        case DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_SLEEP_CONDITION:
            changed_flag = applyTestSleepCondition(human_ptr->data, message_ptr->payload.test_sleep_condition.forbidden_flag);
            plan_mask = DOMINO_HUMAN_PLAN_MASK_TASK;
            break;
        case DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_ENERGY:
            changed_flag = applyTestEnergy(human_ptr->data, message_ptr->payload.test_energy.energy);
            plan_mask = DOMINO_HUMAN_PLAN_MASK_TASK;
            break;
        default:
            /* 登记或撤回外部信息：动机和任务规划器都需要看到新的外部信息。 */
            changed_flag = applyTestStimulus(human_ptr, &message_ptr->payload.test_stimulus);
            plan_mask = DOMINO_HUMAN_PLAN_MASK_ALL;
            break;
    }
    if (changed_flag) {
        dominoHumanMarkPlanPending(human_ptr, plan_mask);
        dominoHumanResolveBoundary(human_ptr);
    }
    finishHuman(human_ptr, now_ms);
}
#endif

/* ---------------------------------------------------------------------------------------------------------------- */
/* 执行线程                                                                                                           */
/* ---------------------------------------------------------------------------------------------------------------- */

static uint64_t writeLock(void) {
    dominoRWLockWriteLock(&domino_human_data_lock);
    return dominoMonotonicTimeNs();
}

static void writeUnlock(uint64_t locked_at_ns) {
    g_execution.stats.write_lock_ns += dominoMonotonicTimeNs() - locked_at_ns;
    dominoRWLockWriteUnlock(&domino_human_data_lock);
}

/** @brief 依次检查动机、任务、行为：每类先处理有限高优先级结果，再处理有限普通结果。
 * @return 是否有结果队列用尽本批配额（可能仍有消息）。
 */
static bool processResultQueues(DominoHumanPlanResultMessage* batch_list) {
    const uint32_t quota = g_domino_human_ai_config.result_quota;
    bool more_flag = false;
    for (uint8_t kind = 0U; kind < DOMINO_HUMAN_PLANNER_COUNT; kind++) {
        for (uint8_t priority = 0U; priority < DOMINO_HUMAN_PLAN_PRIORITY_COUNT; priority++) {
            uint32_t count = 0U;
            const DOMINO_CODE code = dominoThreadQueueConsumeBatch(dominoHumanPlannerResultQueue(kind, priority), batch_list, quota, &count, false);
            if (code != CODE_OK && code != ERR_QUEUE_EMPTY) {
                DOMINO_EXIT_WITH_ERROR(code, "cannot consume plan results kind=%u", kind);
            }
            if (count == 0U) {
                continue;
            }
            const uint64_t locked_at_ns = writeLock();
            for (uint32_t index = 0U; index < count; index++) {
                processResult(kind, priority, &batch_list[index]);
            }
            writeUnlock(locked_at_ns);
            more_flag = more_flag || count == quota;
        }
    }
    return more_flag;
}

/** @brief 按 FIFO 处理有限业务收件。
 * @return 是否用尽本批配额。
 */
static bool processInbox(DominoHumanExecutionMessage* batch_list) {
    const uint32_t quota = g_domino_human_ai_config.inbox_quota;
    uint32_t count = 0U;
    const DOMINO_CODE code = dominoThreadQueueConsumeBatch(&g_execution.inbox, batch_list, quota, &count, false);
    if (code != CODE_OK && code != ERR_QUEUE_EMPTY) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot consume human executor inbox");
    }
    if (count == 0U) {
        return false;
    }
    const uint64_t locked_at_ns = writeLock();
    for (uint32_t index = 0U; index < count; index++) {
        DominoHumanExecutionMessage* message_ptr = &batch_list[index];
        switch (message_ptr->type) {
            case DOMINO_HUMAN_EXECUTION_MESSAGE_GAME_TIMER: {
                DominoHuman* human_ptr = consumeWakeup(&message_ptr->payload.game_timer);
                if (human_ptr) {
                    advanceHumanNow(human_ptr);
                }
                break;
            }
#ifdef DOMINO_PLAYGROUND_MODE
            case DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_SLEEP_CONDITION:
            case DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_ENERGY:
            case DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_STIMULUS:
                processTestInput(message_ptr);
                break;
#endif
            default:
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_MESSAGE, "invalid human executor message type=%d", (int)message_ptr->type);
        }
    }
    writeUnlock(locked_at_ns);
    return count == quota;
}

/** @brief 处理有限本地待办。
 * @return 是否仍有待办。
 */
static bool processTodos(void) {
    if (g_execution.todo_count == 0U) {
        return false;
    }
    const uint64_t locked_at_ns = writeLock();
    for (uint32_t index = 0U; index < g_domino_human_ai_config.todo_quota && g_execution.todo_count != 0U; index++) {
        g_execution.stats.todo_count++;
        advanceHumanNow(popTodo());
    }
    writeUnlock(locked_at_ns);
    return g_execution.todo_count != 0U;
}

/** @brief Snapshot → 退出／冻结检查 → 六条结果队列、业务收件箱、本地待办各自有限配额 → 无工作时按快照等待。 */
static int humanExecutionThread(void* arg_ptr) {
    (void)arg_ptr;
    DominoHumanPlanResultMessage result_batch[DOMINO_HUMAN_EXECUTOR_BATCH_MESSAGE_MAX];
    DominoHumanExecutionMessage inbox_batch[DOMINO_HUMAN_EXECUTOR_BATCH_MESSAGE_MAX];
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(&g_execution.inbox, &epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot snapshot human executor inbox");
        }
        if (dominoEngineIsStopping()) {
            return 0;
        }
        const uint64_t freeze_epoch = dominoTimeModuleGetFreezeEpoch();
        if (freeze_epoch != 0U) {
            dominoTimeModuleWait(freeze_epoch);
            continue;
        }
        g_execution.batch_seq++;
        g_execution.stats.batch_count++;
        const bool results_more_flag = processResultQueues(result_batch);
        const bool inbox_more_flag = processInbox(inbox_batch);
        const bool todo_more_flag = processTodos();
        if (results_more_flag || inbox_more_flag || todo_more_flag) {
            /* 本批额度耗尽但仍有消息或待办：回到批次入口，不进入等待。 */
            continue;
        }
        code = dominoThreadQueueWait(&g_execution.inbox, epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot wait on human executor inbox");
        }
    }
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* 生命周期                                                                                                           */
/* ---------------------------------------------------------------------------------------------------------------- */

void dominoHumanExecutionInit(void) {
    dominoThreadQueueInit(&g_execution.inbox, sizeof(DominoHumanExecutionMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                          DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    dominoHumanPlannerInit(&g_execution.inbox);
}

void dominoHumanExecutionInitAfter(void) {
    const size_t human_count = kv_size(domino_all_human_list);
    if (human_count > DOMINO_HUMAN_WORLD_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "world human count %zu exceeds capacity %u", human_count, DOMINO_HUMAN_WORLD_COUNT_MAX);
    }
    g_execution.todo_capacity = human_count > 0U ? (uint32_t)human_count : 1U;
    domino_human_runtime_list = calloc(g_execution.todo_capacity, sizeof(*domino_human_runtime_list));
    g_execution.todo_ring = calloc(g_execution.todo_capacity, sizeof(*g_execution.todo_ring));
    if (!domino_human_runtime_list || !g_execution.todo_ring) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human runtime arrays count=%zu", human_count);
    }
    /* 计划运行期版本从 1 起重新建立；未回收行为记录数由记录本身重建。 */
    for (size_t index = 0U; index < kv_size(domino_all_task_list); index++) {
        DominoTask* task_ptr = &kv_A(domino_all_task_list, index);
        task_ptr->plan_version = dominoHumanStoreNextPlanVersion();
        if (dominoHumanGetTaskRules(task_ptr->type) == nullptr) {
            DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "task=%u type=%u has no supported rules", task_ptr->task_id, task_ptr->type);
        }
    }
    for (size_t index = 0U; index < kv_size(domino_all_behavior_list); index++) {
        const DominoBehavior* behavior_ptr = &kv_A(domino_all_behavior_list, index);
        if (dominoHumanGetBehaviorRules(behavior_ptr->type) == nullptr) {
            DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "behavior=%u type=%u has no supported rules", behavior_ptr->behavior_id,
                                   behavior_ptr->type);
        }
        dominoGetTaskByID(behavior_ptr->task_id)->record_count++;
    }
    for (size_t index = 0U; index < human_count; index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        human_ptr->state = dominoHumanComputeState(human_ptr);
        /* 待规划标志与下次规划时刻不跨会话保存：每个人物评估一次动机，有有效动机的人物规划一次任务，由规划器重新给出时刻。 */
        dominoHumanMarkPlanPending(human_ptr, (uint8_t)(DOMINO_HUMAN_PLAN_MASK_MOTIVATION |
                                                        (dominoHumanHasActiveMotivation(human_ptr->data) ? DOMINO_HUMAN_PLAN_MASK_TASK : 0U)));
        /* 首次检查机会：Start 后由执行器按有限批次推进、投递规划并登记唤醒；准备阶段不结算、不提交规划。 */
        pushTodo(human_ptr);
    }
}

void dominoHumanExecutionStart(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_execution.thread, humanExecutionThread, nullptr), ERR_THREAD_CREATE,
                                  "cannot create human executor thread");
    dominoHumanPlannerStart();
}

void dominoHumanExecutionNotifyTimeChanged(void) {
    dominoThreadQueueWakeAll(&g_execution.inbox);
    dominoHumanPlannerNotifyTimeChanged();
}

void dominoHumanExecutionStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_execution.thread, nullptr), ERR_THREAD_JOIN, "cannot join human executor thread");
    /* 执行器的末批仍可投递规划请求；先结束生产者，再 join 三个规划线程。 */
    dominoHumanPlannerStop();
}

void dominoHumanExecutionDestroy(void) {
    /* 全部线程已结束且业务事实已保存；留队通知、请求和候选只属于本会话，可由正式事实重建。 */
    dominoHumanPlannerDestroy();
    DominoHumanExecutionMessage message;
    DOMINO_CODE code;
    while ((code = dominoThreadQueueConsume(&g_execution.inbox, &message, false)) == CODE_OK) {
    }
    if (code != ERR_QUEUE_EMPTY) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot drain human executor inbox during destroy");
    }
    dominoThreadQueueDestroy(&g_execution.inbox);
    free(domino_human_runtime_list);
    domino_human_runtime_list = nullptr;
    free(g_execution.todo_ring);
    memset(&g_execution, 0, sizeof(g_execution));
}

void dominoHumanExecutionSubmit(DominoHumanExecutionMessage* message_ptr) {
    const DOMINO_CODE code = dominoThreadQueueProduce(&g_execution.inbox, message_ptr, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot submit human executor message type=%d", (int)message_ptr->type);
    }
}

DominoHumanExecutorStats dominoHumanExecutionGetStats(void) {
    return g_execution.stats;
}

#ifdef DOMINO_PLAYGROUND_MODE
void dominoHumanExecutionProcessResultForTest(uint8_t planner_kind, uint8_t priority, DominoHumanPlanResultMessage* result_ptr) {
    g_execution.batch_seq++;
    processResult(planner_kind, priority, result_ptr);
}

void dominoHumanExecutionAdvanceForTest(domino_human_id_t human_id) {
    g_execution.batch_seq++;
    advanceHumanNow(dominoGetHumanByID(human_id));
}

void dominoHumanExecutionProcessMessageForTest(const DominoHumanExecutionMessage* message_ptr) {
    g_execution.batch_seq++;
    processTestInput(message_ptr);
}
#endif
