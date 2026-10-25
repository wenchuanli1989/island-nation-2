#include "executor.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "../logger/entry.h"
#include "../queue/thread_message.h"
#include "../social/entry.h"
#include "../time/entry.h"
#include "domino_shared_common.h"
#include "domino_shared_queue.h"
#include "entry.h"
#include "needs.h"
#include "planner.h"
#include "rules.h"
#include "store.h"
#include "task/entry.h"
#include "task/sleep.h"
#include "wakeup.h"

KHASHL_MAP_INIT(KH_LOCAL, HumanSleepApprovalMap, humanSleepApprovalMap, domino_human_id_t, domino_building_id_t, kh_hash_uint32, kh_eq_generic)

typedef struct HumanExecution {
    DominoThreadQueue inbox;
    mtx_t mutex;
    cnd_t condition;
    thrd_t thread;
    HumanSleepApprovalMap* approvals_ptr;
    uint64_t command_request_id;
    uint64_t plan_request_id;
    size_t pending_plan_count;
    size_t pending_plan_cursor;
    uint32_t inflight_plan_count;
    domino_task_id_t command_task_id;
    DOMINO_CODE command_result;
    bool ready_flag;  ///< 仅用于首次启动握手，由静态初值或 Destroy 复位。
    bool command_pending_flag;
} HumanExecution;

static HumanExecution g_execution;
enum { HUMAN_EXECUTION_BATCH_COUNT = 64U };

static domino_building_id_t getApproval(domino_human_id_t human_id) {
    const khint_t index = humanSleepApprovalMap_get(g_execution.approvals_ptr, human_id);
    return kh_exist(g_execution.approvals_ptr, index) ? kh_val(g_execution.approvals_ptr, index) : 0U;
}

static const char* planReasonName(DominoHumanPlanReason reason) {
    switch (reason) {
        case DOMINO_HUMAN_PLAN_REASON_LOW_ENERGY:
            return "low_energy";
        case DOMINO_HUMAN_PLAN_REASON_ENERGY_SUFFICIENT:
            return "energy_sufficient";
        case DOMINO_HUMAN_PLAN_REASON_NO_AUTHORIZATION:
            return "no_authorization";
        case DOMINO_HUMAN_PLAN_REASON_BUSY:
            return "busy";
        case DOMINO_HUMAN_PLAN_REASON_CAPACITY:
            return "capacity";
        case DOMINO_HUMAN_PLAN_REASON_TIME_RANGE:
            return "time_range";
    }
    return "invalid_reason";
}

/** @brief 仅记录请求的状态变化；DEBUG 关闭时不构造逐人历史或轮询日志。 */
static void logPlanEvent(domino_human_id_t human_id, uint64_t request_id, const char* outcome_ptr, const char* reason_ptr, DOMINO_CODE code) {
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                      "plan human=%" PRIu32 " request=%" PRIu64 " outcome=%s reason=%s code=%d", human_id, request_id, outcome_ptr, reason_ptr,
                      (int)code);
}

static void markPlanPending(DominoHuman* human_ptr, uint32_t reasons) {
    if (!human_ptr->plan_submit_pending_flag) {
        human_ptr->plan_submit_pending_flag = true;
        g_execution.pending_plan_count++;
    }
    human_ptr->data->pending_plan_reasons |= reasons;
}

static void clearPendingPlan(DominoHuman* human_ptr) {
    if (human_ptr->plan_submit_pending_flag) {
        assert(g_execution.pending_plan_count > 0U);
        g_execution.pending_plan_count--;
        human_ptr->plan_submit_pending_flag = false;
    }
    human_ptr->data->pending_plan_reasons = 0U;
}

/** @brief 本批自主安排不穿越任何既有非终态任务，也不把建筑存在当成许可。 */
static bool canPlanSleep(const DominoHuman* human_ptr, domino_building_id_t approval) {
    const DominoHumanData* data_ptr = human_ptr->data;
    if (approval == 0U || data_ptr->current_building_id != approval || !dominoGetBuildingByID(approval) ||
        data_ptr->task_count >= DOMINO_HUMAN_TASK_COUNT_MAX) {
        return false;
    }
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        if (data_ptr->active_task_id_list[channel] != 0U) {
            return false;
        }
    }
    for (uint8_t index = 0U; index < data_ptr->task_count; index++) {
        if (dominoGetTaskByID(data_ptr->task_id_list[index])->status < DOMINO_TASK_STATUS_SUCCEEDED) {
            return false;
        }
    }
    return true;
}

/** @brief 无活动请求的人物成功入队才占用信用；满窗口的去重意图由结果消费归还信用后补投。 */
static void requestSleepPlan(DominoHuman* human_ptr, domino_building_id_t approval, domino_runtime_ms_t now_ms) {
    const bool already_pending_flag = human_ptr->plan_submit_pending_flag;
    markPlanPending(human_ptr, DOMINO_HUMAN_PLAN_TRIGGER_ENERGY);
    if (g_execution.inflight_plan_count == DOMINO_HUMAN_PLAN_INFLIGHT_MAX) {
        if (!already_pending_flag && g_execution.inflight_plan_count == DOMINO_HUMAN_PLAN_INFLIGHT_MAX) {
            logPlanEvent(human_ptr->human_id, 0U, "pending", "inflight_window_full", CODE_OK);
        }
        return;
    }
    if (g_execution.plan_request_id == UINT64_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "plan request IDs exhausted human=%" PRIu32, human_ptr->human_id);
    }
    const DominoHumanData* data_ptr = human_ptr->data;
    const DominoHumanPlanRequest request = {
        .request_id = ++g_execution.plan_request_id,
        .human_id = human_ptr->human_id,
        .schedule_revision = data_ptr->schedule_revision,
        .now_ms = now_ms,
        .reasons = data_ptr->pending_plan_reasons,
        .current_building_id = data_ptr->current_building_id,
        .approved_sleep_building_id = approval,
        .energy = data_ptr->energy,
        .task_count = data_ptr->task_count,
    };
    dominoHumanPlannerSubmit(&request);
    human_ptr->active_plan_request_id = request.request_id;
    g_execution.inflight_plan_count++;
    clearPendingPlan(human_ptr);
}

/** @brief 当前只接独立、无停止交接的单行为睡眠；其他合法类型仍留在 store。 */
static bool isCoreSleep(const DominoTask* task_ptr) {
    return task_ptr->type == DOMINO_TASK_TYPE_SLEEP && task_ptr->behavior_count == 1U &&
           task_ptr->behaviors_ptr[0].type == DOMINO_BEHAVIOR_TYPE_SLEEP && task_ptr->parent_task_id == 0U && task_ptr->active_child_task_id == 0U &&
           task_ptr->pending_child_task_id == 0U && task_ptr->cancel_target_task_id == 0U && task_ptr->pending_stop_reason == DOMINO_TASK_STOP_NONE &&
           task_ptr->cancel_scope == DOMINO_TASK_CANCEL_NONE;
}

/** @brief 校验活动槽并返回已定位任务；调用方不得跨任务池增删保留该指针。 */
static DominoTask* getActiveTask(const DominoHumanData* data_ptr) {
    DominoTask* active_task_ptr = nullptr;
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        const domino_task_id_t task_id = data_ptr->active_task_id_list[channel];
        if (task_id == 0U) {
            continue;
        }
        DominoTask* task_ptr = dominoGetTaskByID(task_id);
        if (!task_ptr || task_ptr->owner_human_id != data_ptr->human_id || task_ptr->channel_index != channel ||
            task_ptr->status != DOMINO_TASK_STATUS_RUNNING) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "human=%" PRIu32 " active task=%" PRIu32 " channel=%u has missing or inconsistent task data",
                                   data_ptr->human_id, task_id, (unsigned)channel);
        }
        if (active_task_ptr || !isCoreSleep(task_ptr)) {
            DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "human=%" PRIu32 " active task=%" PRIu32 " exceeds executor core sleep support",
                                   data_ptr->human_id, task_id);
        }
        if (task_ptr->behaviors_ptr[0].progress.status != DOMINO_BEHAVIOR_STATUS_ACTIVE) {
            DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID,
                                   "human=%" PRIu32 " running task=%" PRIu32 " has no active sleep behavior supported by executor",
                                   data_ptr->human_id, task_id);
        }
        active_task_ptr = task_ptr;
    }
    return active_task_ptr;
}

/** @brief 在取得写入所有权后检查显式许可；已保存的目标从不被当作许可来源。 */
static DOMINO_CODE validateApproval(domino_human_id_t human_id, domino_building_id_t building_id, DominoHuman** human_out_ptr) {
    if (human_id == 0U || building_id == 0U) {
        return ERR_INVALID_PARAM;
    }
    DominoHuman* human_ptr = dominoGetHumanByID(human_id);
    if (!human_ptr || !dominoGetBuildingByID(building_id)) {
        return ERR_NOT_FOUND;
    }
    if (!human_ptr->data) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "sleep approval human=%" PRIu32 " has no bound data", human_id);
    }
    if (human_ptr->data->current_building_id != building_id) {
        return ERR_GAME_STATE_INVALID;
    }
    const DominoTask* active_task_ptr = getActiveTask(human_ptr->data);
    if (active_task_ptr) {
        const domino_building_id_t old_approval = getApproval(human_id);
        if (active_task_ptr->behaviors_ptr[0].target_building_id != building_id || (old_approval != 0U && old_approval != building_id)) {
            return ERR_GAME_STATE_INVALID;
        }
    }
    *human_out_ptr = human_ptr;
    return CODE_OK;
}

static void validateFrozenWorld(void) {
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    dominoHumanStoreValidate(now_ms);
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        const DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        const DominoHumanData* data_ptr = human_ptr->data;
        if (data_ptr != &kv_A(domino_all_human_data_list, index) || data_ptr->human_id != human_ptr->human_id) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "human data binding mismatch human=%" PRIu32 " index=%zu", human_ptr->human_id, index);
        }
        /* 绑定指向 StoreValidate 刚校验的同索引 data；此处只补充执行器支持范围。 */
        const DominoTask* active_task_ptr = nullptr;
        for (uint8_t task_index = 0U; task_index < data_ptr->task_count; task_index++) {
            const DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[task_index]);
            if (task_ptr->status == DOMINO_TASK_STATUS_SUSPENDED) {
                DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "human executor cannot resume suspended task=%" PRIu32 " human=%" PRIu32,
                                       task_ptr->task_id, human_ptr->human_id);
            }
            if (task_ptr->status == DOMINO_TASK_STATUS_RUNNING) {
                if (active_task_ptr || !isCoreSleep(task_ptr) || task_ptr->behaviors_ptr[0].progress.status != DOMINO_BEHAVIOR_STATUS_ACTIVE) {
                    DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID,
                                           "human executor cannot restore running task=%" PRIu32 " human=%" PRIu32 " as a sole active core sleep",
                                           task_ptr->task_id, human_ptr->human_id);
                }
                active_task_ptr = task_ptr;
            }
        }
        if (!active_task_ptr) {
            continue;
        }
        const DominoBehavior* behavior_ptr = &active_task_ptr->behaviors_ptr[0];
        const domino_building_id_t approval = getApproval(human_ptr->human_id);
        if (approval == 0U || approval != behavior_ptr->target_building_id || approval != data_ptr->current_building_id ||
            behavior_ptr->progress.accounted_at_ms != now_ms || now_ms - data_ptr->needs_updated_at_ms >= DOMINO_HUMAN_NEED_MINUTE_MS) {
            DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID,
                                   "human=%" PRIu32 " active sleep approval or frozen settlement is inconsistent at now_ms=%" PRIu32,
                                   human_ptr->human_id, now_ms);
        }
        const domino_runtime_ms_t remaining_ms = behavior_ptr->parameters.sleep.target_active_ms - behavior_ptr->progress.active_elapsed_ms;
        if (remaining_ms > UINT32_MAX - now_ms) {
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "human=%" PRIu32 " restored sleep deadline overflows now_ms=%" PRIu32 " remaining_ms=%" PRIu32,
                                   human_ptr->human_id, now_ms, remaining_ms);
        }
    }
}

void dominoHumanExecutionSettle(domino_runtime_ms_t now_ms) {
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        DominoTask* active_task_ptr = getActiveTask(human_ptr->data);
        const DOMINO_CODE code = active_task_ptr
                                     ? dominoHumanAdvanceSleepTask(active_task_ptr, human_ptr->data, getApproval(human_ptr->human_id), now_ms)
                                     : dominoHumanSettleNeeds(human_ptr->data, now_ms);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot settle frozen human=%" PRIu32 " now_ms=%" PRIu32, human_ptr->human_id, now_ms);
        }
    }
}

/** @brief 独立条件变量交付命令结果；调用时持命令锁，不依赖客户端消费业务消息。 */
static void finishCommandLocked(uint64_t request_id, DOMINO_CODE code, domino_task_id_t task_id) {
    if (g_execution.command_pending_flag && request_id == g_execution.command_request_id) {
        g_execution.command_result = code;
        g_execution.command_task_id = task_id;
        g_execution.command_pending_flag = false;
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&g_execution.condition), ERR_CONDITION_BROADCAST, "human executor:cnd_broadcast");
    }
}

/** @brief 正常停止时完成尚未执行的命令回执；队列保留到所有投递线程退出。 */
static int completeExecutionThread(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_execution.mutex), ERR_MUTEX_LOCK, "human executor:mtx_lock");
    if (g_execution.command_pending_flag) {
        finishCommandLocked(g_execution.command_request_id, ERR_GAME_STATE_INVALID, 0U);
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_execution.mutex), ERR_MUTEX_UNLOCK, "human executor:mtx_unlock");
    return 0;
}

/** @brief 对已定位人物推进一次短步；人物池在执行期不增删，任务池增删后只按 ID 重新定位。 */
static void advanceHuman(DominoHuman* human_ptr) {
    const domino_human_id_t human_id = human_ptr->human_id;
    const domino_building_id_t approval = getApproval(human_id);
    DominoHumanData* data_ptr = human_ptr->data;
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    DOMINO_CODE code;
    DominoTask* active_task_ptr = getActiveTask(data_ptr);
    domino_task_id_t active_task_id = 0U;
    if (active_task_ptr) {
        code = dominoHumanAdvanceSleepTask(active_task_ptr, data_ptr, approval, now_ms);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot advance human=%" PRIu32, human_id);
        }
        active_task_id = data_ptr->active_task_id_list[active_task_ptr->channel_index];
    }
    domino_task_id_t planned_task_id = 0U;
    domino_runtime_ms_t planned_begin_ms = 0U;
    for (uint8_t index = 0U; index < data_ptr->task_count;) {
        const DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[index]);
        if (isCoreSleep(task_ptr) && task_ptr->status >= DOMINO_TASK_STATUS_SUCCEEDED) {
            code = dominoHumanStoreRemoveTask(task_ptr->task_id);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "cannot advance human=%" PRIu32, human_id);
            }
            continue;  // 删除会移动任务池；下一次重新按 ID 取指针。
        }
        if (isCoreSleep(task_ptr) && task_ptr->status == DOMINO_TASK_STATUS_PLANNED && approval == task_ptr->behaviors_ptr[0].target_building_id) {
            const domino_runtime_ms_t begin_ms = task_ptr->planned_begin_ms > task_ptr->behaviors_ptr[0].planned_begin_ms
                                                     ? task_ptr->planned_begin_ms
                                                     : task_ptr->behaviors_ptr[0].planned_begin_ms;
            if (planned_task_id == 0U || begin_ms < planned_begin_ms) {
                planned_task_id = task_ptr->task_id;
                planned_begin_ms = begin_ms;
            }
        }
        index++;
    }
    /* 推进只会清空原活动槽，回收只删除终态任务；无须再次扫描和校验活动槽。 */
    active_task_ptr = active_task_id != 0U ? dominoGetTaskByID(active_task_id) : nullptr;
    if (active_task_id == 0U && planned_task_id != 0U && planned_begin_ms <= now_ms) {
        DominoTask* planned_task_ptr = dominoGetTaskByID(planned_task_id);
        code = dominoHumanStartSleepTask(planned_task_ptr, data_ptr, approval, now_ms);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot advance human=%" PRIu32, human_id);
        }
        active_task_ptr = planned_task_ptr;
    }
    if (active_task_ptr) {
        clearPendingPlan(human_ptr);
        const DominoBehavior* behavior_ptr = &active_task_ptr->behaviors_ptr[0];
        const domino_runtime_ms_t remaining_ms = behavior_ptr->parameters.sleep.target_active_ms - behavior_ptr->progress.active_elapsed_ms;
        if (remaining_ms > UINT32_MAX - behavior_ptr->progress.accounted_at_ms) {
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "human=%" PRIu32 " sleep deadline overflows accounted_at_ms=%" PRIu32 " remaining_ms=%" PRIu32,
                                   human_id, behavior_ptr->progress.accounted_at_ms, remaining_ms);
        }
        dominoHumanWakeupSchedule(human_ptr, behavior_ptr->progress.accounted_at_ms + remaining_ms);
        return;
    }
    if (planned_task_id != 0U) {
        clearPendingPlan(human_ptr);
        dominoHumanWakeupSchedule(human_ptr, planned_begin_ms);
        return;
    }
    code = dominoHumanSettleNeeds(data_ptr, now_ms);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot advance human=%" PRIu32, human_id);
    }
    if (!canPlanSleep(human_ptr, approval)) {
        clearPendingPlan(human_ptr);
        dominoHumanWakeupCancel(human_ptr);
        return;
    }
    if (human_ptr->active_plan_request_id != 0U) {
        dominoHumanWakeupCancel(human_ptr);
        return;
    }
    domino_runtime_ms_t trigger_ms;
    code = dominoHumanGetSleepTriggerMs(data_ptr, now_ms, &trigger_ms);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot advance human=%" PRIu32, human_id);
    }
    if (trigger_ms > now_ms) {
        clearPendingPlan(human_ptr);
        const domino_timer_id_t old_timer_id = human_ptr->wakeup_timer_id;
        dominoHumanWakeupSchedule(human_ptr, trigger_ms);
        if (human_ptr->wakeup_timer_id != old_timer_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                              "sleep threshold human=%" PRIu32 " reason=energy_sufficient energy=%u due_ms=%" PRIu32, human_id,
                              (unsigned)data_ptr->energy, trigger_ms);
        }
        return;
    }
    dominoHumanWakeupCancel(human_ptr);
    requestSleepPlan(human_ptr, approval, now_ms);
}

/** @brief 结果只终结本次请求；版本或资格失效时保留一次重新采样意图。 */
static void applyPlanResult(DominoHumanPlanEnvelope* envelope_ptr) {
    if (g_execution.inflight_plan_count == 0U) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "received human plan result without an inflight request");
    }
    g_execution.inflight_plan_count--;
    const DominoHumanPlanRequest* request_ptr = &envelope_ptr->request;
    if (envelope_ptr->status != DOMINO_HUMAN_PLAN_DRAFT && envelope_ptr->status != DOMINO_HUMAN_PLAN_NO_PLAN) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_MESSAGE, "unknown plan result status=%d human=%" PRIu32 " request=%" PRIu64, (int)envelope_ptr->status,
                               request_ptr->human_id, request_ptr->request_id);
    }
    DominoHuman* human_ptr = dominoGetHumanByID(request_ptr->human_id);
    if (!human_ptr || human_ptr->active_plan_request_id != request_ptr->request_id) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "stale", "request_identity", CODE_OK);
        return;
    }
    human_ptr->active_plan_request_id = 0U;
    if (envelope_ptr->status == DOMINO_HUMAN_PLAN_NO_PLAN) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "no_plan", planReasonName(envelope_ptr->reason), CODE_OK);
        /* 无计划是终结结果；等待实际条件事件，不能立即重复同一失败候选。 */
        clearPendingPlan(human_ptr);
        return;
    }
    if (envelope_ptr->task.owner_human_id != request_ptr->human_id || envelope_ptr->task.channel_index != DOMINO_HUMAN_TASK_CHANNEL_PRIMARY) {
        DOMINO_EXIT_WITH_ERROR(
            ERR_INVALID_DATA, "plan draft owner/channel differs from request human=%" PRIu32 " request=%" PRIu64 " owner=%" PRIu32 " channel=%u",
            request_ptr->human_id, request_ptr->request_id, envelope_ptr->task.owner_human_id, (unsigned)envelope_ptr->task.channel_index);
    }
    const domino_building_id_t approval = getApproval(human_ptr->human_id);
    if (human_ptr->data->schedule_revision != request_ptr->schedule_revision || approval != request_ptr->approved_sleep_building_id ||
        !canPlanSleep(human_ptr, approval)) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "stale", "schedule_or_eligibility_changed", CODE_OK);
        markPlanPending(human_ptr, request_ptr->reasons);
        return;
    }
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    DOMINO_CODE code = dominoHumanSettleNeeds(human_ptr->data, now_ms);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot apply human plan result human=%" PRIu32 " request=%" PRIu64, request_ptr->human_id,
                               request_ptr->request_id);
    }
    domino_task_id_t task_id;
    code = dominoHumanCommitSleepTaskDraft(&envelope_ptr->task, human_ptr->data, approval, request_ptr->schedule_revision, now_ms, &task_id);
    if (code == ERR_GAME_STATE_INVALID || code == ERR_ALREADY_EXISTS || code == ERR_NOT_FOUND) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "rejected", "commit_eligibility_changed", code);
        markPlanPending(human_ptr, request_ptr->reasons);
        return;
    }
    if (code == ERR_OUT_OF_RANGE || code == ERR_CAPACITY_EXCEEDED) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "rejected", "time_or_capacity_limit", code);
        clearPendingPlan(human_ptr);
        return;
    }
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot apply human plan result human=%" PRIu32 " request=%" PRIu64, request_ptr->human_id,
                               request_ptr->request_id);
    }
    logPlanEvent(request_ptr->human_id, request_ptr->request_id, "accepted", "low_energy", CODE_OK);
    advanceHuman(human_ptr);
}

/** @brief 业务提交成功后实际写入许可；普通拒绝返回命令结果，分配失败直接终止。 */
static DOMINO_CODE applyCommand(const HumanExecutionCommand* command_ptr, domino_runtime_ms_t now_ms, domino_task_id_t* task_id_out_ptr,
                                DominoHuman** human_out_ptr) {
    DominoHuman* human_ptr;
    DOMINO_CODE code = validateApproval(command_ptr->human_id, command_ptr->building_id, &human_ptr);
    if (code != CODE_OK) {
        return code;
    }
    khint_t approval_index = humanSleepApprovalMap_get(g_execution.approvals_ptr, command_ptr->human_id);
    const bool new_approval_flag = !kh_exist(g_execution.approvals_ptr, approval_index);
    if (new_approval_flag) {
        const khint_t capacity = kh_capacity(g_execution.approvals_ptr);
        if (kh_size(g_execution.approvals_ptr) >= kh_max_count(capacity)) {
            if (capacity >= ((khint_t)1U << 31U)) {
                return ERR_CAPACITY_EXCEEDED;
            }
            const khint_t new_capacity = capacity == 0U ? 4U : capacity * 2U;
            if ((size_t)new_capacity > SIZE_MAX / sizeof(HumanSleepApprovalMap_m_bucket_t)) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "sleep approval map allocation size overflows capacity=%u", (unsigned)new_capacity);
            }
        }
    }
    if (command_ptr->type == HUMAN_EXECUTION_SUBMIT_SLEEP) {
        code = dominoHumanSubmitSleepTask(human_ptr->data, command_ptr->building_id, command_ptr->channel_index, now_ms, task_id_out_ptr);
        if (code == ERR_INVALID_DATA) {
            DOMINO_EXIT_WITH_ERROR(code, "invalid human data during sleep submission human=%" PRIu32, command_ptr->human_id);
        }
        if (code != CODE_OK) {
            return code;
        }
    }
    if (new_approval_flag) {
        int absent_flag;
        approval_index = humanSleepApprovalMap_put(g_execution.approvals_ptr, command_ptr->human_id, &absent_flag);
        if (absent_flag < 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot insert sleep approval human=%" PRIu32, command_ptr->human_id);
        }
    }
    kh_val(g_execution.approvals_ptr, approval_index) = command_ptr->building_id;
    *human_out_ptr = human_ptr;
    return CODE_OK;
}

static void processMessage(DominoThreadMessage* message_ptr) {
    if (message_ptr->type == DOMINO_THREAD_MESSAGE_COMMAND) {
        const HumanExecutionCommand command = message_ptr->payload.command;
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_execution.mutex), ERR_MUTEX_LOCK, "human executor:mtx_lock");
        const bool accepted_flag = g_execution.command_pending_flag && command.request_id == g_execution.command_request_id;
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_execution.mutex), ERR_MUTEX_UNLOCK, "human executor:mtx_unlock");
        if (!accepted_flag) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_MESSAGE, "human command does not match pending request human=%" PRIu32 " request=%" PRIu64,
                                   command.human_id, command.request_id);
        }
        domino_task_id_t task_id = 0U;
        DominoHuman* human_ptr = nullptr;
        domino_runtime_ms_t now_ms;
        dominoTimeGetRuntimeMs(&now_ms);
        const DOMINO_CODE code = applyCommand(&command, now_ms, &task_id, &human_ptr);
        if (code == CODE_OK) {
            advanceHuman(human_ptr);
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_execution.mutex), ERR_MUTEX_LOCK, "human executor:mtx_lock");
        finishCommandLocked(command.request_id, code, task_id);
        DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_execution.mutex), ERR_MUTEX_UNLOCK, "human executor:mtx_unlock");
    } else if (message_ptr->type == DOMINO_THREAD_MESSAGE_AI_RESULT) {
        applyPlanResult(&message_ptr->payload.plan);
        dominoHumanPlannerReleaseMessage(message_ptr);
    } else if (message_ptr->type == DOMINO_THREAD_MESSAGE_TIMER) {
        DominoHuman* human_ptr;
        const DOMINO_CODE code = dominoHumanWakeupConsume(&message_ptr->payload.timer, &human_ptr);
        if (code == CODE_OK) {
            advanceHuman(human_ptr);
        } else if (code != ERR_NOT_FOUND) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot consume human wakeup message");
        }
    } else {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_MESSAGE, "invalid human executor message type=%d", (int)message_ptr->type);
    }
}

static int humanExecutionThread(void* arg_ptr) {
    (void)arg_ptr;
    size_t startup_index = 0U;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_execution.mutex), ERR_MUTEX_LOCK, "human executor:mtx_lock");
    g_execution.ready_flag = true;
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&g_execution.condition), ERR_CONDITION_BROADCAST, "human executor:cnd_broadcast");
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_execution.mutex), ERR_MUTEX_UNLOCK, "human executor:mtx_unlock");
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(&g_execution.inbox, &epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot snapshot human executor inbox");
        }
        if (dominoEngineIsStopping()) {
            return completeExecutionThread();
        }
        if (dominoTimeModuleIsFrozen()) {
            code = dominoThreadQueueWait(&g_execution.inbox, epoch);
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "cannot wait on frozen human executor inbox");
            }
            continue;
        }
        /* 一轮最多处理三组各 64 项；进入后完整执行，普通冻结和退出只在下一轮观察。 */
        uint32_t consumed_count = 0U;
        for (; consumed_count < HUMAN_EXECUTION_BATCH_COUNT; consumed_count++) {
            DominoThreadMessage message;
            code = dominoThreadQueueConsume(&g_execution.inbox, &message, false);
            if (code == ERR_QUEUE_EMPTY) {
                break;
            }
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "cannot consume human executor inbox");
            }
            processMessage(&message);
        }
        /* 仅首次启动建立调度；暂停保留游标，普通恢复不会重新扫描已经处理的人物。 */
        uint32_t startup_count = 0U;
        for (; startup_index < kv_size(domino_all_human_list) && startup_count < HUMAN_EXECUTION_BATCH_COUNT; startup_count++) {
            DominoHuman* human_ptr = &kv_A(domino_all_human_list, startup_index++);
            advanceHuman(human_ptr);
        }
        uint32_t pending_scanned_count = 0U;
        for (; g_execution.pending_plan_count != 0U && g_execution.inflight_plan_count < DOMINO_HUMAN_PLAN_INFLIGHT_MAX &&
               pending_scanned_count < HUMAN_EXECUTION_BATCH_COUNT;
             pending_scanned_count++) {
            if (g_execution.pending_plan_cursor >= kv_size(domino_all_human_list)) {
                g_execution.pending_plan_cursor = 0U;
            }
            DominoHuman* human_ptr = &kv_A(domino_all_human_list, g_execution.pending_plan_cursor++);
            if (!human_ptr->plan_submit_pending_flag) {
                continue;
            }
            advanceHuman(human_ptr);
        }
        if (consumed_count == HUMAN_EXECUTION_BATCH_COUNT || startup_count == HUMAN_EXECUTION_BATCH_COUNT ||
            pending_scanned_count == HUMAN_EXECUTION_BATCH_COUNT) {
            continue;
        }
        code = dominoThreadQueueWait(&g_execution.inbox, epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot wait on human executor inbox");
        }
    }
}

void dominoHumanExecutionInit(void) {
    g_execution.approvals_ptr = humanSleepApprovalMap_init();
    if (!g_execution.approvals_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human sleep approval map");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_init(&g_execution.mutex, mtx_plain), ERR_MUTEX_INIT, "cannot initialize human executor mutex");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&g_execution.condition), ERR_CONDITION_INIT, "cannot initialize human executor command condition");
    dominoThreadQueueInit(&g_execution.inbox, sizeof(DominoThreadMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    dominoHumanWakeupInit(&g_execution.inbox);
    dominoHumanPlannerInit(&g_execution.inbox);
}

void dominoHumanExecutionDestroy(void) {
    /* 已启动的 planner 和 timer 均已 join，不会再收到投递。 */
    dominoThreadQueueBeginFlush(&g_execution.inbox);
    DominoThreadMessage message;
    DOMINO_CODE code;
    while ((code = dominoThreadQueueConsume(&g_execution.inbox, &message, false)) == CODE_OK) {
        dominoHumanPlannerReleaseMessage(&message);
    }
    if (code != ERR_QUEUE_FLUSHING) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot drain human executor inbox during destroy");
    }
    dominoThreadQueueDestroy(&g_execution.inbox);
    dominoHumanWakeupDestroy();
    cnd_destroy(&g_execution.condition);
    mtx_destroy(&g_execution.mutex);
    humanSleepApprovalMap_destroy(g_execution.approvals_ptr);
    memset(&g_execution, 0, sizeof(g_execution));
}

void dominoHumanExecutionStart(void) {
    validateFrozenWorld();
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_execution.thread, humanExecutionThread, nullptr), ERR_THREAD_CREATE,
                                  "cannot create human executor thread");
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_execution.mutex), ERR_MUTEX_LOCK, "human executor:mtx_lock");
    while (!g_execution.ready_flag) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&g_execution.condition, &g_execution.mutex), ERR_CONDITION_WAIT, "human executor:cnd_wait");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_execution.mutex), ERR_MUTEX_UNLOCK, "human executor:mtx_unlock");
}

void dominoHumanExecutionNotifyTimeChanged(void) {
    dominoThreadQueueWakeAll(&g_execution.inbox);
    dominoHumanPlannerNotifyTimeChanged();
}

void dominoHumanExecutionStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_execution.thread, nullptr), ERR_THREAD_JOIN, "cannot join human executor thread");
}

/** @brief 公开入口已确认客户端线程和已初始化生命周期；此处只检查动态命令状态。 */
static DOMINO_CODE submitCommand(HumanExecutionCommand command, domino_task_id_t* task_id_out_ptr) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_execution.mutex), ERR_MUTEX_LOCK, "human executor:mtx_lock");
    DOMINO_CODE code;
    domino_task_id_t task_id = 0U;
    if (!dominoEngineIsRunning()) {
        domino_runtime_ms_t now_ms;
        dominoTimeGetRuntimeMs(&now_ms);
        DominoHuman* human_ptr;
        code = applyCommand(&command, now_ms, &task_id, &human_ptr);
        goto unlock;
    }
    if (dominoTimeModuleIsFrozen() || g_execution.command_pending_flag) {
        code = ERR_GAME_STATE_INVALID;
        goto unlock;
    }
    if (g_execution.command_request_id == UINT64_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "human command request IDs exhausted");
    }
    command.request_id = ++g_execution.command_request_id;
    DominoThreadMessage message = {.type = DOMINO_THREAD_MESSAGE_COMMAND, .payload.command = command};
    g_execution.command_pending_flag = true;
    code = dominoThreadQueueProduce(&g_execution.inbox, &message, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot deliver human command human=%" PRIu32 " request=%" PRIu64, command.human_id, command.request_id);
    }
    while (g_execution.command_pending_flag) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&g_execution.condition, &g_execution.mutex), ERR_CONDITION_WAIT, "human executor:cnd_wait");
    }
    code = g_execution.command_result;
    task_id = g_execution.command_task_id;
unlock:
    if (code == CODE_OK && task_id_out_ptr) {
        *task_id_out_ptr = task_id;
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_execution.mutex), ERR_MUTEX_UNLOCK, "human executor:mtx_unlock");
    return code;
}

DOMINO_CODE dominoHumanExecutionAuthorizeSleep(domino_human_id_t human_id, domino_building_id_t building_id) {
    return submitCommand((HumanExecutionCommand){.human_id = human_id, .building_id = building_id, .type = HUMAN_EXECUTION_AUTHORIZE_SLEEP}, nullptr);
}

DOMINO_CODE dominoHumanExecutionSubmitSleep(domino_human_id_t human_id, domino_building_id_t building_id, uint8_t channel_index,
                                            domino_task_id_t* task_id_out_ptr) {
    if (!task_id_out_ptr) {
        return ERR_NULL_POINTER;
    }
    return submitCommand(
        (HumanExecutionCommand){
            .human_id = human_id, .building_id = building_id, .type = HUMAN_EXECUTION_SUBMIT_SLEEP, .channel_index = channel_index},
        task_id_out_ptr);
}
