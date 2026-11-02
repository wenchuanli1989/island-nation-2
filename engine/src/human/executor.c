#include "executor.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "../game_timer/entry.h"
#include "../logger/entry.h"
#include "../queue/message.h"
#include "../time/entry.h"
#include "domino_shared_common.h"
#include "domino_shared_queue.h"
#include "entry.h"
#include "needs.h"
#include "planner.h"
#include "rules.h"
#include "state.h"
#include "store.h"
#include "task/entry.h"

/** @file executor.c
 * @brief human 世界的唯一运行期写入者；内部规划结果和到期通知从 inbox 进入。
 * @note 每人最多一个有效规划请求，直接入队，等待规划结果消息推进。
 * 任务失败时同步设置人物异常，可信加载保留该状态；不反复扫描任务推导人物状态。
 * 计时器只用于任务开始、任务结束或精力阈值；等待规划结果由消息驱动。
 */

typedef struct HumanExecution {
    DominoThreadQueue inbox;
    thrd_t thread;
} HumanExecution;

/** @brief 本次选择同时带回任务、执行接口和开始时刻；不跨任务池增删保留。 */
typedef struct TaskSelection {
    DominoTask* task_ptr;
    const DominoTaskOps* ops_ptr;
    domino_runtime_ms_t begin_ms;
} TaskSelection;

static HumanExecution g_execution;
enum {
    HUMAN_EXECUTION_BATCH_COUNT = 64U,
    HUMAN_WAKEUP_EVENT_TYPE = 1U,
};

/** @brief 执行线程先撤销当前唤醒身份，再标记取消；已出队的旧通知由消费端忽略。 */
static void cancelWakeup(DominoHuman* human_ptr) {
    const domino_game_timer_id_t game_timer_id = human_ptr->wakeup_game_timer_id;
    if (game_timer_id == 0U) {
        return;
    }
    human_ptr->wakeup_game_timer_id = 0U;
    human_ptr->wakeup_due_ms = 0U;
    const DOMINO_CODE code = dominoGameTimerCancel(game_timer_id, &g_execution.inbox);
    if (code != CODE_OK && code != ERR_NOT_FOUND) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot cancel human=%" PRIu32 " wakeup game_timer=%" PRIu64, human_ptr->human_id, game_timer_id);
    }
}

/** @brief 为人物登记唯一绝对截止点；同一有效截止点复用原登记，0 毫秒同样合法。
 * @return 是否建立了新登记；业务调用方据此记录日志，加载准备不记录业务日志。
 */
static bool scheduleWakeup(DominoHuman* human_ptr, domino_runtime_ms_t due_ms) {
    if (human_ptr->wakeup_game_timer_id != 0U && human_ptr->wakeup_due_ms == due_ms) {
        return false;
    }
    cancelWakeup(human_ptr);
    const DOMINO_CODE code =
        dominoGameTimerScheduleAt(&g_execution.inbox, due_ms, HUMAN_WAKEUP_EVENT_TYPE, human_ptr->human_id, &human_ptr->wakeup_game_timer_id);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot schedule human=%" PRIu32 " wakeup due_ms=%" PRIu32, human_ptr->human_id, due_ms);
    }
    human_ptr->wakeup_due_ms = due_ms;
    return true;
}

/** @brief 按 data_id 定位人物，仅消费当前 game_timer_id；旧通知不能清除后来登记的唤醒。
 * @pre 消息来自私有 inbox，计时器原样回传本模块登记的人物 ID 和事件类型。
 */
static DominoHuman* consumeWakeup(const DominoGameTimerRequestMessage* game_timer_ptr) {
    DominoHuman* human_ptr = dominoGetHumanByID((domino_human_id_t)game_timer_ptr->data_id);
    if (!human_ptr || human_ptr->wakeup_game_timer_id != game_timer_ptr->game_timer_id) {
        return nullptr;
    }
    human_ptr->wakeup_game_timer_id = 0U;
    human_ptr->wakeup_due_ms = 0U;
    return game_timer_ptr->cancelled_flag ? nullptr : human_ptr;
}

/** @brief 仅记录请求的状态变化；DEBUG 关闭时不构造逐人历史或轮询日志。 */
static void logPlanEvent(domino_human_id_t human_id, uint64_t request_id, const char* outcome_ptr, const char* reason_ptr, DOMINO_CODE code) {
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                      "plan human=%" PRIu32 " request=%" PRIu64 " outcome=%s reason=%s code=%d", human_id, request_id, outcome_ptr, reason_ptr,
                      (int)code);
}

/** @brief 异常人物停止调度；已发请求的结果失效，草案仍由结果消费者释放。 */
static void discardHumanScheduling(DominoHuman* human_ptr) {
    cancelWakeup(human_ptr);
    human_ptr->active_plan_request_id = 0U;
}

/** @brief 已提交任务的业务失败立即终结任务并隔离人物；资源及协议故障仍由各入口致命退出。 */
static void failTask(DominoHuman* human_ptr, DominoTask* task_ptr, domino_runtime_ms_t now_ms, DOMINO_CODE failure_code) {
    if (failure_code != ERR_GAME_STATE_INVALID && failure_code != ERR_OUT_OF_RANGE) {
        DOMINO_EXIT_WITH_ERROR(failure_code, "invalid execution state for task=%" PRIu32 " human=%" PRIu32, task_ptr->task_id, human_ptr->human_id);
    }
    task_ptr->status = DOMINO_TASK_STATUS_FAILED;
    task_ptr->ended_at_ms = now_ms;
    DominoBehavior* behavior_ptr = dominoGetBehaviorByID(task_ptr->current_behavior_id);
    behavior_ptr->status = DOMINO_BEHAVIOR_STATUS_FAILED;
    behavior_ptr->ended_at_ms = now_ms;
    // 启动失败时该槽原本为空，推进失败时该槽只属于当前任务。
    human_ptr->data->active_task_id_list[task_ptr->channel_index] = 0U;
    human_ptr->status = DOMINO_HUMAN_STATUS_EXCEPTION;
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                      "task failed human=%" PRIu32 " task=%" PRIu32 " code=%d; human entered exception state", human_ptr->human_id, task_ptr->task_id,
                      (int)failure_code);
}

/** @brief 睡眠固定主通道，主通道须无未终结任务；次通道任务不阻止提交。 */
static bool canPlanSleep(const DominoHuman* human_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    if (data_ptr->task_count >= DOMINO_HUMAN_TASK_COUNT_MAX) {
        return false;
    }
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        const DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (task_ptr->status < DOMINO_TASK_STATUS_SUCCEEDED && task_ptr->channel_index == DOMINO_HUMAN_TASK_CHANNEL_PRIMARY) {
            return false;
        }
    }
    return true;
}

/** @brief 取得已实现且支持当前任务形态的执行接口；未接入的任务仍可留在 store。 */
static const DominoTaskOps* getTaskOps(const DominoTask* task_ptr) {
    const DominoTaskOps* ops_ptr = dominoHumanGetTaskOps(task_ptr->type);
    return ops_ptr && ops_ptr->supports(task_ptr) ? ops_ptr : nullptr;
}

/** @brief 同一人物已有活动任务使用此类型时，其他通道不能再启动同类型任务。 */
static bool isTaskTypeRunning(const DominoHumanData* data_ptr, DOMINO_TASK_TYPE type) {
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        const domino_task_id_t active_id = data_ptr->active_task_id_list[channel];
        if (active_id != 0U && dominoGetTaskByID(active_id)->type == type) {
            return true;
        }
    }
    return false;
}

/** @brief 按各任务的效果边界推进到同一时刻；自然需求每个边界只结算一次，回调只结算自身效果。
 * @param finish_all_flag Stop 使用 true，单项失败后仍结算其他活动通道和自然需求；运行期失败则立即隔离人物。
 * @note 本过程不增删任务池、不启动下一任务、不登记计时器。
 */
static bool settleHumanTasks(DominoHuman* human_ptr, domino_runtime_ms_t now_ms, bool finish_all_flag) {
    DominoHumanData* data_ptr = human_ptr->data;
    if (now_ms < data_ptr->needs_updated_at_ms) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "human=%" PRIu32 " time precedes need settlement", human_ptr->human_id);
    }
    DominoTask* active_tasks[DOMINO_HUMAN_TASK_CHANNEL_COUNT] = {0};
    const DominoTaskOps* active_ops[DOMINO_HUMAN_TASK_CHANNEL_COUNT] = {0};
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        const domino_task_id_t task_id = data_ptr->active_task_id_list[channel];
        if (task_id == 0U) {
            continue;
        }
        active_tasks[channel] = dominoGetTaskByID(task_id);
        active_ops[channel] = getTaskOps(active_tasks[channel]);
        if (!active_ops[channel]) {
            DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "human=%" PRIu32 " active task=%" PRIu32 " has no supported executor", human_ptr->human_id,
                                   task_id);
        }
    }
    bool succeeded_flag = true;
    for (;;) {
        domino_runtime_ms_t step_ms = now_ms;
        for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
            if (!active_tasks[channel]) {
                continue;
            }
            domino_runtime_ms_t settle_ms;
            domino_runtime_ms_t wakeup_ms;
            const DominoBehavior* behavior_ptr = dominoGetBehaviorByID(active_tasks[channel]->current_behavior_id);
            active_ops[channel]->get_times(active_tasks[channel], behavior_ptr, &settle_ms, &wakeup_ms);
            if (settle_ms <= behavior_ptr->accounted_at_ms || settle_ms < data_ptr->needs_updated_at_ms) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "task=%" PRIu32 " returned a non-advancing settlement time", active_tasks[channel]->task_id);
            }
            if (settle_ms < step_ms) {
                step_ms = settle_ms;
            }
        }
        dominoHumanApplyNaturalNeeds(data_ptr, step_ms);
        for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
            DominoTask* task_ptr = active_tasks[channel];
            if (!task_ptr) {
                continue;
            }
            const DOMINO_CODE code = active_ops[channel]->advance(task_ptr, human_ptr, step_ms);
            if (code != CODE_OK) {
                failTask(human_ptr, task_ptr, step_ms, code);
                if (!finish_all_flag) {
                    return false;
                }
                succeeded_flag = false;
                active_tasks[channel] = nullptr;
                continue;
            }
            if (task_ptr->status == DOMINO_TASK_STATUS_SUCCEEDED) {
                data_ptr->active_task_id_list[channel] = 0U;
                active_tasks[channel] = nullptr;
            }
        }
        if (step_ms == now_ms) {
            return succeeded_flag;
        }
    }
}

void dominoHumanExecutionSettle(domino_runtime_ms_t now_ms) {
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        /* 异常只阻止运行期继续调度；已经开始的其他通道及自然需求仍须结算到停钟时刻。 */
        settleHumanTasks(human_ptr, now_ms, true);
    }
}

/** @brief 回收成功终态，无需再次查询执行接口；删除会移动任务和行为池，不跨此调用保留池内指针。 */
static void recycleCompletedTasks(DominoHumanData* data_ptr) {
    for (uint8_t index = 0U; index < data_ptr->task_count;) {
        const DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (task_ptr->status == DOMINO_TASK_STATUS_SUCCEEDED) {
            dominoHumanStoreRecycleTask(data_ptr, index);
            continue;
        }
        index++;
    }
}

/** @brief 从空通道选出最早可执行的任务，同刻按人物清单顺序；正在运行的同类型任务不能成为候选。
 * @pre 调用方已确认本通道空闲。
 */
static TaskSelection selectNextTask(const DominoHumanData* data_ptr, uint8_t channel_index) {
    TaskSelection selected = {0};
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (task_ptr->channel_index != channel_index || task_ptr->status != DOMINO_TASK_STATUS_PLANNED ||
            isTaskTypeRunning(data_ptr, task_ptr->type)) {
            continue;
        }
        const DominoTaskOps* ops_ptr = getTaskOps(task_ptr);
        if (!ops_ptr) {
            continue;
        }
        const DominoBehavior* behavior_ptr = dominoGetBehaviorByID(task_ptr->current_behavior_id);
        const domino_runtime_ms_t begin_ms =
            task_ptr->planned_begin_ms > behavior_ptr->planned_begin_ms ? task_ptr->planned_begin_ms : behavior_ptr->planned_begin_ms;
        if (!selected.task_ptr || begin_ms < selected.begin_ms) {
            selected = (TaskSelection){.task_ptr = task_ptr, .ops_ptr = ops_ptr, .begin_ms = begin_ms};
        }
    }
    return selected;
}

/** @brief 主通道优先，各空槽分别启动已到时任务；同类型冲突保留为 PLANNED，业务失败隔离人物。 */
static bool startReadyTasks(DominoHuman* human_ptr, domino_runtime_ms_t now_ms) {
    DominoHumanData* data_ptr = human_ptr->data;
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        if (data_ptr->active_task_id_list[channel] != 0U) {
            continue;
        }
        const TaskSelection selected = selectNextTask(data_ptr, channel);
        if (!selected.task_ptr || selected.begin_ms > now_ms) {
            continue;
        }
        DominoTask* task_ptr = selected.task_ptr;
        const DOMINO_CODE code = selected.ops_ptr->start(task_ptr, human_ptr, now_ms);
        if (code != CODE_OK) {
            failTask(human_ptr, task_ptr, now_ms, code);
            return false;
        }
        task_ptr->status = DOMINO_TASK_STATUS_RUNNING;
        task_ptr->started_flag = true;
        task_ptr->started_at_ms = now_ms;
        data_ptr->active_task_id_list[channel] = task_ptr->task_id;
    }
    return true;
}

/** @brief 从业务事实计算最近截止点；调用方已确认所有活动任务具有支持的执行接口。 */
static bool getHumanWakeup(const DominoHuman* human_ptr, domino_runtime_ms_t* due_ms_out_ptr) {
    const DominoHumanData* data_ptr = human_ptr->data;
    bool has_wakeup_flag = false;
    domino_runtime_ms_t due_ms = 0U;
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        domino_runtime_ms_t channel_due_ms;
        const domino_task_id_t task_id = data_ptr->active_task_id_list[channel];
        if (task_id != 0U) {
            const DominoTask* task_ptr = dominoGetTaskByID(task_id);
            const DominoBehavior* behavior_ptr = dominoGetBehaviorByID(task_ptr->current_behavior_id);
            domino_runtime_ms_t settle_ms;
            // 活动任务已在加载准备、本轮结算或启动时确认支持，无需重复检查形态。
            dominoHumanGetTaskOps(task_ptr->type)->get_times(task_ptr, behavior_ptr, &settle_ms, &channel_due_ms);
        } else {
            const TaskSelection selected = selectNextTask(data_ptr, channel);
            if (!selected.task_ptr) {
                continue;
            }
            channel_due_ms = selected.begin_ms;
        }
        if (!has_wakeup_flag || channel_due_ms < due_ms) {
            due_ms = channel_due_ms;
            has_wakeup_flag = true;
        }
    }
    if (human_ptr->active_plan_request_id == 0U && data_ptr->energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY && canPlanSleep(human_ptr)) {
        const domino_runtime_ms_t trigger_ms = dominoHumanGetSleepTriggerMs(data_ptr);
        if (!has_wakeup_flag || trigger_ms < due_ms) {
            due_ms = trigger_ms;
            has_wakeup_flag = true;
        }
    }
    *due_ms_out_ptr = due_ms;
    return has_wakeup_flag;
}

/** @brief 合并三个通道的结束／开始和精力阈值，只保留人物最近的一个唤醒；相同时刻复用原计时器。 */
static void updateHumanWakeup(DominoHuman* human_ptr) {
    domino_runtime_ms_t due_ms;
    const bool has_wakeup_flag = getHumanWakeup(human_ptr, &due_ms);
    if (has_wakeup_flag) {
        if (scheduleWakeup(human_ptr, due_ms)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "human wakeup human=%" PRIu32 " due_ms=%" PRIu32,
                              human_ptr->human_id, due_ms);
        }
    } else {
        cancelWakeup(human_ptr);
    }
}

void dominoHumanExecutionInitAfter(void) {
    static_assert(HUMAN_EXECUTION_BATCH_COUNT <= DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, "initial scheduling batches must fit the timer queue");
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    uint32_t prepared_count = 0U;
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        const DominoHumanData* data_ptr = human_ptr->data;
        /* 异常人物仍可能留下需要在 Stop 结算的活动通道，执行能力也必须在 Start 前准备好。 */
        for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
            const domino_task_id_t task_id = data_ptr->active_task_id_list[channel];
            if (task_id != 0U && !getTaskOps(dominoGetTaskByID(task_id))) {
                DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "human=%" PRIu32 " active task=%" PRIu32 " has no supported executor",
                                       human_ptr->human_id, task_id);
            }
        }
        if (human_ptr->status == DOMINO_HUMAN_STATUS_EXCEPTION) {
            continue;
        }
        bool immediate_flag = data_ptr->energy <= DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY && canPlanSleep(human_ptr);
        for (uint8_t task_index = 0U; task_index < data_ptr->task_count && !immediate_flag; task_index++) {
            immediate_flag = dominoGetTaskByID(data_ptr->task_id_list[task_index])->status == DOMINO_TASK_STATUS_SUCCEEDED;
        }
        domino_runtime_ms_t due_ms;
        if (immediate_flag) {
            /* 留队规划可以重做；终态回收及新规划均由启动后的正常业务处理，不在加载时改变事实。 */
            scheduleWakeup(human_ptr, now_ms);
        } else if (getHumanWakeup(human_ptr, &due_ms)) {
            scheduleWakeup(human_ptr, due_ms);
        } else {
            continue;
        }
        if (++prepared_count == HUMAN_EXECUTION_BATCH_COUNT) {
            dominoGameTimerPrepareRequests();
            prepared_count = 0U;
        }
    }
    dominoGameTimerPrepareRequests();
}

/** @brief 已完成共同结算，只复核当前睡眠资格；其他通道的日程变动不要求重做睡眠草案。
 * @pre 请求身份已匹配；草案由受控 planner 构造且独占，不重复验证其中不变的人物归属、类型和通道。
 */
static void applyPlanResult(DominoHuman* human_ptr, const DominoHumanPlanResult* result_ptr, domino_runtime_ms_t now_ms) {
    const DominoHumanPlanRequestMessage* request_ptr = &result_ptr->request;
    switch (result_ptr->outcome) {
        case DOMINO_HUMAN_PLAN_TIME_RANGE:
        case DOMINO_HUMAN_PLAN_STATE_CHANGED:
            logPlanEvent(request_ptr->human_id, request_ptr->request_id, "no_plan",
                         result_ptr->outcome == DOMINO_HUMAN_PLAN_TIME_RANGE ? "time_range" : "state_changed", CODE_OK);
            return;
        case DOMINO_HUMAN_PLAN_DRAFT:
            break;
        default:
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_MESSAGE, "unknown plan result outcome=%d human=%" PRIu32 " request=%" PRIu64, (int)result_ptr->outcome,
                                   request_ptr->human_id, request_ptr->request_id);
    }
    if (!canPlanSleep(human_ptr)) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "stale", "sleep_no_longer_eligible", CODE_OK);
        return;
    }

    // 预检防止过时草案入池后立即启动失败；任务启动仍独立校验已持有任务的业务条件。
    domino_runtime_ms_t planned_end_ms;
    DOMINO_CODE code = dominoHumanPrepareSleep(human_ptr->data->energy, now_ms, &planned_end_ms);
    if (code == CODE_OK) {
        code = dominoHumanStoreAddValidatedTask(&result_ptr->draft, human_ptr->data);
    }
    logPlanEvent(request_ptr->human_id, request_ptr->request_id, code == CODE_OK ? "accepted" : "rejected", "current_sleep_conditions", code);
}

/** @brief 到期通知和规划结果共用一次推进：结算 → 回收 → 提交草案 → 启动 → 更新调度。
 * @param plan_ptr 非空表示已匹配身份的规划结果；仅 STATE_CHANGED 允许按本轮重新结算的需求再次规划。
 * 时长越界及草案拒绝不立即重试，避免在条件未变时循环提交。
 */
static void advanceHuman(DominoHuman* human_ptr, const DominoHumanPlanResult* plan_ptr) {
    if (human_ptr->status == DOMINO_HUMAN_STATUS_EXCEPTION) {
        return;
    }
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    if (!settleHumanTasks(human_ptr, now_ms, false)) {
        goto failed;
    }
    recycleCompletedTasks(human_ptr->data);
    if (plan_ptr) {
        applyPlanResult(human_ptr, plan_ptr, now_ms);
    }
    if (!startReadyTasks(human_ptr, now_ms)) {
        goto failed;
    }
    const bool can_request_plan_flag = !plan_ptr || plan_ptr->outcome == DOMINO_HUMAN_PLAN_STATE_CHANGED;
    if (can_request_plan_flag && human_ptr->active_plan_request_id == 0U && human_ptr->data->energy <= DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY &&
        canPlanSleep(human_ptr)) {
        // 本批写锁覆盖入队及身份发布，规划器取得读锁后才检查此身份。
        human_ptr->active_plan_request_id = dominoHumanPlannerSubmit(human_ptr->human_id);
    }
    updateHumanWakeup(human_ptr);
    return;

failed:
    // 仅业务推进撤销调度；Stop 复用共同结算时不再访问已停止的计时器。
    discardHumanScheduling(human_ptr);
}

/** @brief 按消息用途分发；草案行为块始终由结果消费者释放。 */
static void processMessage(DominoHumanExecutionMessage* message_ptr) {
    switch (message_ptr->type) {
        case DOMINO_HUMAN_EXECUTION_MESSAGE_PLAN_RESULT: {
            const DominoHumanPlanRequestMessage* request_ptr = &message_ptr->payload.plan.request;
            DominoHuman* human_ptr = dominoGetHumanByID(request_ptr->human_id);
            if (human_ptr && human_ptr->active_plan_request_id == request_ptr->request_id) {
                human_ptr->active_plan_request_id = 0U;
                advanceHuman(human_ptr, &message_ptr->payload.plan);
            } else {
                logPlanEvent(request_ptr->human_id, request_ptr->request_id, "stale", "request_identity", CODE_OK);
            }
            free(message_ptr->payload.plan.draft.behaviors_ptr);
            return;
        }
        case DOMINO_HUMAN_EXECUTION_MESSAGE_GAME_TIMER: {
            DominoHuman* human_ptr = consumeWakeup(&message_ptr->payload.game_timer);
            if (human_ptr) {
                advanceHuman(human_ptr, nullptr);
            }
            return;
        }
        default:
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_MESSAGE, "invalid human executor message type=%d", (int)message_ptr->type);
    }
}

static int humanExecutionThread(void* arg_ptr) {
    (void)arg_ptr;
    DominoHumanExecutionMessage messages[HUMAN_EXECUTION_BATCH_COUNT];
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
        /* 初始调度已在 InitAfter 建立；每轮最多处理 64 条消息，冻结和退出只在下一轮观察。 */
        uint32_t consumed_count;
        code = dominoThreadQueueConsumeBatch(&g_execution.inbox, messages, HUMAN_EXECUTION_BATCH_COUNT, &consumed_count, false);
        if (code != CODE_OK && code != ERR_QUEUE_EMPTY) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot consume human executor inbox");
        }
        if (consumed_count != 0U) {
            // 出队已经释放队列锁；一次写锁覆盖本批消息及池布局变化。
            dominoRWLockWriteLock(&domino_human_data_lock);
            for (uint32_t index = 0U; index < consumed_count; index++) {
                processMessage(&messages[index]);
            }
            dominoRWLockWriteUnlock(&domino_human_data_lock);
        }
        if (consumed_count == HUMAN_EXECUTION_BATCH_COUNT) {
            continue;
        }
        code = dominoThreadQueueWait(&g_execution.inbox, epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot wait on human executor inbox");
        }
    }
}

void dominoHumanExecutionInit(void) {
    dominoThreadQueueInit(&g_execution.inbox, sizeof(DominoHumanExecutionMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                          DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    dominoHumanPlannerInit(&g_execution.inbox);
}

void dominoHumanExecutionDestroy(void) {
    /* 全部线程已结束且业务事实已保存；计时通知可重建，未提交草案可按人物需求重做。 */
    dominoHumanPlannerDestroy();
    DominoHumanExecutionMessage message;
    DOMINO_CODE code;
    while ((code = dominoThreadQueueConsume(&g_execution.inbox, &message, false)) == CODE_OK) {
        if (message.type == DOMINO_HUMAN_EXECUTION_MESSAGE_PLAN_RESULT) {
            free(message.payload.plan.draft.behaviors_ptr);
        }
    }
    if (code != ERR_QUEUE_EMPTY) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot drain human executor inbox during destroy");
    }
    dominoThreadQueueDestroy(&g_execution.inbox);
    memset(&g_execution, 0, sizeof(g_execution));
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
    /* 执行器最后一批仍可提交规划；先结束生产者，再 join 规划线程。 */
    dominoHumanPlannerStop();
}
