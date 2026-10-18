#include "executor.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdckdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "../entry.h"
#include "../logger/entry.h"
#include "../queue/thread_message.h"
#include "../queue/thread_queue.h"
#include "../social/entry.h"
#include "../time/entry.h"
#include "entry.h"
#include "needs.h"
#include "planner.h"
#include "rules.h"
#include "store.h"
#include "task/entry.h"
#include "task/sleep.h"
#include "wakeup.h"

KHASHL_MAP_INIT(KH_LOCAL, HumanSleepApprovalMap, humanSleepApprovalMap, domino_human_id_t, domino_building_id_t, kh_hash_uint32, kh_eq_generic)

typedef enum HumanExecutionCommandType {
    HUMAN_EXECUTION_AUTHORIZE_SLEEP,
    HUMAN_EXECUTION_SUBMIT_SLEEP,
} HumanExecutionCommandType;

/** @brief COMMAND 只携带独占值；结果通过控制条件变量返回，不借用客户端输出地址。 */
typedef struct HumanExecutionCommand {
    uint64_t request_id;
    domino_human_id_t human_id;
    domino_building_id_t building_id;
    HumanExecutionCommandType type;
    uint8_t channel_index;
} HumanExecutionCommand;

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
    DOMINO_CODE failure_code;
    DOMINO_CODE stop_result;
    atomic_bool ready_flag;
    atomic_bool stop_requested_flag;
    atomic_bool stop_completed_flag;
    bool initialized_flag;
    bool started_flag;
    bool command_pending_flag;
} HumanExecution;

static HumanExecution g_execution;
enum { HUMAN_EXECUTION_BATCH_COUNT = 64U };

/** @brief 执行收件箱在线程退出前保持双端开放，意外关闭属于实例故障。 */
static void logUnexpectedInboxStop(DOMINO_CODE code, const char* operation_ptr) {
    if (code == ERR_QUEUE_PRODUCER_STOPPED || code == ERR_QUEUE_CONSUMER_STOPPED) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human executor inbox unexpectedly stopped during %s code=%d",
                          operation_ptr, (int)code);
    }
}

/** @brief 与 queue/timer 相同，同步原语自身失败不能继续假装持锁或完成握手。 */
static void checkThreadResult(int result) {
    if (result != thrd_success) {
        (void)fprintf(stderr, "domino human executor: synchronization failure (%d)\n", result);
        abort();
    }
}

/** @brief 等待启动／停止回执；实例首错不依赖业务队列投递。 */
static DOMINO_CODE waitLifecycleFlag(const atomic_bool* completed_flag_ptr) {
    const struct timespec interval = {.tv_nsec = 10000000L};
    for (;;) {
        const DOMINO_CODE code = dominoEngineGetLifecycleError();
        if (code != CODE_OK) {
            return code;
        }
        if (atomic_load(completed_flag_ptr)) {
            return CODE_OK;
        }
        if (thrd_sleep(&interval, nullptr) == -2) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "executor lifecycle acknowledgement wait sleep failed");
            return dominoEngineReportFailure(ERR_THREAD_SLEEP);
        }
    }
}

/** @brief 命令回执最多等待 10 ms 后复查跨模块首错；普通空闲线程仍使用事件等待。 */
static DOMINO_CODE getCommandWaitDeadline(struct timespec* deadline_out_ptr) {
    if (timespec_get(deadline_out_ptr, TIME_UTC) != TIME_UTC) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot read UTC clock for human command acknowledgement");
        return ERR_CLOCK_READ;
    }
    deadline_out_ptr->tv_nsec += 10000000L;
    if (deadline_out_ptr->tv_nsec >= 1000000000L) {
        if (ckd_add(&deadline_out_ptr->tv_sec, deadline_out_ptr->tv_sec, (time_t)1)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human command acknowledgement deadline seconds overflow");
            return ERR_OUT_OF_RANGE;
        }
        deadline_out_ptr->tv_nsec -= 1000000000L;
    }
    return CODE_OK;
}

/** @brief 调用方持命令锁；超时仅用于复查全局故障，时钟失败返回调用方终结本次握手。 */
static DOMINO_CODE waitCommandConditionLocked(void) {
    struct timespec deadline;
    DOMINO_CODE code = getCommandWaitDeadline(&deadline);
    if (code != CODE_OK) {
        return code;
    }
    const int result = cnd_timedwait(&g_execution.condition, &g_execution.mutex, &deadline);
    if (result != thrd_timedout) {
        checkThreadResult(result);
    }
    return CODE_OK;
}

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
        case DOMINO_HUMAN_PLAN_REASON_SYSTEM_FAILURE:
            return "system_failure";
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
static DOMINO_CODE requestSleepPlan(DominoHuman* human_ptr, domino_building_id_t approval, domino_runtime_ms_t now_ms) {
    const bool already_pending_flag = human_ptr->plan_submit_pending_flag;
    markPlanPending(human_ptr, DOMINO_HUMAN_PLAN_TRIGGER_ENERGY);
    if (g_execution.inflight_plan_count == DOMINO_HUMAN_PLAN_INFLIGHT_MAX) {
        if (!already_pending_flag && g_execution.inflight_plan_count == DOMINO_HUMAN_PLAN_INFLIGHT_MAX) {
            logPlanEvent(human_ptr->human_id, 0U, "pending", "inflight_window_full", CODE_OK);
        }
        return CODE_OK;
    }
    if (g_execution.plan_request_id == UINT64_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "plan request IDs exhausted human=%" PRIu32, human_ptr->human_id);
        return ERR_ID_EXHAUSTED;
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
    const DOMINO_CODE code = dominoHumanPlannerSubmit(&request);
    if (code != CODE_OK) {
        logPlanEvent(human_ptr->human_id, request.request_id, "failed", "request_submission", code);
        return code;
    }
    human_ptr->active_plan_request_id = request.request_id;
    g_execution.inflight_plan_count++;
    clearPendingPlan(human_ptr);
    return CODE_OK;
}

/** @brief 当前只接独立、无停止交接的单行为睡眠；其他合法类型仍留在 store。 */
static bool isCoreSleep(const DominoTask* task_ptr) {
    return task_ptr->type == DOMINO_TASK_TYPE_SLEEP && task_ptr->behavior_count == 1U &&
           task_ptr->behaviors_ptr[0].type == DOMINO_BEHAVIOR_TYPE_SLEEP && task_ptr->parent_task_id == 0U && task_ptr->active_child_task_id == 0U &&
           task_ptr->pending_child_task_id == 0U && task_ptr->cancel_target_task_id == 0U && task_ptr->pending_stop_reason == DOMINO_TASK_STOP_NONE &&
           task_ptr->cancel_scope == DOMINO_TASK_CANCEL_NONE;
}

/** @brief 校验活动槽并返回已定位任务；调用方不得跨任务池增删保留该指针。 */
static DOMINO_CODE getActiveTask(const DominoHumanData* data_ptr, DominoTask** task_out_ptr) {
    DominoTask* active_task_ptr = nullptr;
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        const domino_task_id_t task_id = data_ptr->active_task_id_list[channel];
        if (task_id == 0U) {
            continue;
        }
        DominoTask* task_ptr = dominoGetTaskByID(task_id);
        if (!task_ptr || task_ptr->owner_human_id != data_ptr->human_id || task_ptr->channel_index != channel ||
            task_ptr->status != DOMINO_TASK_STATUS_RUNNING) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "human=%" PRIu32 " active task=%" PRIu32 " channel=%u has missing or inconsistent task data", data_ptr->human_id,
                              task_id, (unsigned)channel);
            return ERR_INVALID_DATA;
        }
        if (active_task_ptr || !isCoreSleep(task_ptr)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "human=%" PRIu32 " active task=%" PRIu32 " exceeds executor core sleep support", data_ptr->human_id, task_id);
            return ERR_GAME_STATE_INVALID;
        }
        if (task_ptr->behaviors_ptr[0].progress.status != DOMINO_BEHAVIOR_STATUS_ACTIVE) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "human=%" PRIu32 " running task=%" PRIu32 " has no active sleep behavior supported by executor", data_ptr->human_id,
                              task_id);
            return ERR_GAME_STATE_INVALID;
        }
        active_task_ptr = task_ptr;
    }
    *task_out_ptr = active_task_ptr;
    return CODE_OK;
}

/** @brief 在取得写入所有权后检查显式许可；已保存的目标从不被当作许可来源。 */
static DOMINO_CODE validateApproval(domino_human_id_t human_id, domino_building_id_t building_id, DominoHuman** human_out_ptr) {
    if (!dominoHumanIdMap || !dominoBuildingIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "sleep approval requires initialized human and building maps");
        return ERR_NOT_INITIALIZED;
    }
    if (human_id == 0U || building_id == 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "sleep approval requires nonzero human and building IDs human=%" PRIu32 " building=%" PRIu32, human_id, building_id);
        return ERR_INVALID_PARAM;
    }
    DominoHuman* human_ptr = dominoGetHumanByID(human_id);
    if (!human_ptr || !dominoGetBuildingByID(building_id)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "sleep approval human or building not found human=%" PRIu32 " building=%" PRIu32, human_id, building_id);
        return ERR_NOT_FOUND;
    }
    if (!human_ptr->data) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "sleep approval human=%" PRIu32 " has no bound data", human_id);
        return ERR_INVALID_DATA;
    }
    if (human_ptr->data->current_building_id != building_id) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "sleep approval human=%" PRIu32 " is in building=%" PRIu32 " instead of approved building=%" PRIu32, human_id,
                          human_ptr->data->current_building_id, building_id);
        return ERR_GAME_STATE_INVALID;
    }
    DominoTask* active_task_ptr;
    DOMINO_CODE code = getActiveTask(human_ptr->data, &active_task_ptr);
    if (code != CODE_OK) {
        return code;
    }
    if (active_task_ptr) {
        const domino_building_id_t old_approval = getApproval(human_id);
        if (active_task_ptr->behaviors_ptr[0].target_building_id != building_id || (old_approval != 0U && old_approval != building_id)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "sleep approval conflicts with active task human=%" PRIu32 " task=%" PRIu32 " building=%" PRIu32, human_id,
                              active_task_ptr->task_id, building_id);
            return ERR_GAME_STATE_INVALID;
        }
    }
    *human_out_ptr = human_ptr;
    return CODE_OK;
}

static DOMINO_CODE validateFrozenWorld(void) {
    if (!dominoTimeModuleIsFrozen()) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human executor startup requires a frozen world");
        return ERR_GAME_STATE_INVALID;
    }
    domino_runtime_ms_t now_ms;
    DOMINO_CODE code = dominoTimeGetRuntimeMs(&now_ms);
    if (code != CODE_OK) {
        return code;
    }
    code = dominoHumanStoreValidate(now_ms);
    if (code != CODE_OK) {
        return code;
    }
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        const DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        const DominoHumanData* data_ptr = human_ptr->data;
        if (data_ptr != &kv_A(domino_all_human_data_list, index) || data_ptr->human_id != human_ptr->human_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human data binding mismatch human=%" PRIu32 " index=%zu",
                              human_ptr->human_id, index);
            return ERR_INVALID_DATA;
        }
        /* 绑定指向 StoreValidate 刚校验的同索引 data；此处只补充执行器支持范围。 */
        const DominoTask* active_task_ptr = nullptr;
        for (uint8_t task_index = 0U; task_index < data_ptr->task_count; task_index++) {
            const DominoTask* task_ptr = dominoGetTaskByID(data_ptr->task_id_list[task_index]);
            if (task_ptr->status == DOMINO_TASK_STATUS_SUSPENDED) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "human executor cannot resume suspended task=%" PRIu32 " human=%" PRIu32, task_ptr->task_id, human_ptr->human_id);
                return ERR_GAME_STATE_INVALID;
            }
            if (task_ptr->status == DOMINO_TASK_STATUS_RUNNING) {
                if (active_task_ptr || !isCoreSleep(task_ptr) || task_ptr->behaviors_ptr[0].progress.status != DOMINO_BEHAVIOR_STATUS_ACTIVE) {
                    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                      "human executor cannot restore running task=%" PRIu32 " human=%" PRIu32 " as a sole active core sleep",
                                      task_ptr->task_id, human_ptr->human_id);
                    return ERR_GAME_STATE_INVALID;
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
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "human=%" PRIu32 " active sleep approval or frozen settlement is inconsistent at now_ms=%" PRIu32, human_ptr->human_id,
                              now_ms);
            return ERR_GAME_STATE_INVALID;
        }
        const domino_runtime_ms_t remaining_ms = behavior_ptr->parameters.sleep.target_active_ms - behavior_ptr->progress.active_elapsed_ms;
        if (remaining_ms > UINT32_MAX - now_ms) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "human=%" PRIu32 " restored sleep deadline overflows now_ms=%" PRIu32 " remaining_ms=%" PRIu32, human_ptr->human_id,
                              now_ms, remaining_ms);
            return ERR_OUT_OF_RANGE;
        }
    }
    return CODE_OK;
}

/** @brief 结算到冻结时刻；首错立即停止，失败实例不再尝试形成可保存快照。 */
static DOMINO_CODE settleFrozenWorld(domino_runtime_ms_t now_ms) {
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DOMINO_CODE code = dominoEngineGetLifecycleError();
        if (code != CODE_OK) {
            return code;
        }
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        DominoTask* active_task_ptr;
        code = getActiveTask(human_ptr->data, &active_task_ptr);
        if (code != CODE_OK) {
            return code;
        }
        code = active_task_ptr ? dominoHumanAdvanceSleepTask(active_task_ptr, human_ptr->data, getApproval(human_ptr->human_id), now_ms)
                               : dominoHumanSettleNeeds(human_ptr->data, now_ms);
        if (code != CODE_OK) {
            return code;
        }
    }
    return CODE_OK;
}

/** @brief 独立条件变量交付命令结果；调用时持命令锁，不依赖客户端消费业务消息。 */
static void finishCommandLocked(uint64_t request_id, DOMINO_CODE code, domino_task_id_t task_id) {
    if (g_execution.command_pending_flag && request_id == g_execution.command_request_id) {
        g_execution.command_result = code;
        g_execution.command_task_id = task_id;
        g_execution.command_pending_flag = false;
        checkThreadResult(cnd_broadcast(&g_execution.condition));
    }
}

/** @brief 调用方持命令锁；首错终结实例并完成命令回执，不释放仍被线程使用的资源。 */
static void failExecutionLocked(DOMINO_CODE code) {
    code = dominoEngineReportFailure(code);
    if (g_execution.failure_code == CODE_OK) {
        g_execution.failure_code = code;
    }
    atomic_store(&g_execution.stop_requested_flag, true);
    finishCommandLocked(g_execution.command_request_id, g_execution.failure_code, 0U);
    checkThreadResult(cnd_broadcast(&g_execution.condition));
}

void dominoHumanExecutionRequestFailure(DOMINO_CODE code) {
    code = dominoEngineReportFailure(code);
    if (!g_execution.initialized_flag) {
        return;
    }
    checkThreadResult(mtx_lock(&g_execution.mutex));
    failExecutionLocked(code);
    checkThreadResult(mtx_unlock(&g_execution.mutex));
    dominoThreadQueueWakeAll(&g_execution.inbox);
    dominoHumanPlannerRequestFailure(code);
}

static void failExecution(DOMINO_CODE code) {
    dominoHumanExecutionRequestFailure(code);
}

/** @brief 线程唯一退出回执；收件队列保留到规划和 timer 投递者都已结束，错误时保留实例资源。 */
static DOMINO_CODE completeExecutionThread(DOMINO_CODE code) {
    const DOMINO_CODE engine_code = dominoEngineGetLifecycleError();
    if (engine_code != CODE_OK) {
        code = engine_code;
    }
    checkThreadResult(mtx_lock(&g_execution.mutex));
    if (code != CODE_OK) {
        failExecutionLocked(code);
    }
    if (g_execution.failure_code != CODE_OK) {
        code = g_execution.failure_code;
    }
    const DOMINO_CODE pending_code = code == CODE_OK ? ERR_GAME_STATE_INVALID : code;
    if (code == CODE_OK && g_execution.command_pending_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "human executor stopped before command request=%" PRIu64 " completed", g_execution.command_request_id);
    }
    finishCommandLocked(g_execution.command_request_id, pending_code, 0U);
    g_execution.stop_result = code;
    checkThreadResult(cnd_broadcast(&g_execution.condition));
    checkThreadResult(mtx_unlock(&g_execution.mutex));
    atomic_store(&g_execution.stop_completed_flag, true);
    return code;
}

/** @brief 对已定位人物推进一次短步；人物池在执行期不增删，任务池增删后只按 ID 重新定位。 */
static DOMINO_CODE advanceHuman(DominoHuman* human_ptr) {
    const DOMINO_CODE engine_code = dominoEngineGetLifecycleError();
    if (engine_code != CODE_OK) {
        return engine_code;
    }
    const domino_human_id_t human_id = human_ptr->human_id;
    const domino_building_id_t approval = getApproval(human_id);
    DominoHumanData* data_ptr = human_ptr->data;
    domino_runtime_ms_t now_ms;
    DOMINO_CODE code = dominoTimeGetRuntimeMs(&now_ms);
    if (code != CODE_OK) {
        return code;
    }
    DominoTask* active_task_ptr;
    code = getActiveTask(data_ptr, &active_task_ptr);
    if (code != CODE_OK) {
        return code;
    }
    domino_task_id_t active_task_id = 0U;
    if (active_task_ptr) {
        code = dominoHumanAdvanceSleepTask(active_task_ptr, data_ptr, approval, now_ms);
        if (code != CODE_OK) {
            return code;
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
                return code;
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
            return code;
        }
        active_task_ptr = planned_task_ptr;
    }
    if (active_task_ptr) {
        clearPendingPlan(human_ptr);
        const DominoBehavior* behavior_ptr = &active_task_ptr->behaviors_ptr[0];
        const domino_runtime_ms_t remaining_ms = behavior_ptr->parameters.sleep.target_active_ms - behavior_ptr->progress.active_elapsed_ms;
        if (remaining_ms > UINT32_MAX - behavior_ptr->progress.accounted_at_ms) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "human=%" PRIu32 " sleep deadline overflows accounted_at_ms=%" PRIu32 " remaining_ms=%" PRIu32, human_id,
                              behavior_ptr->progress.accounted_at_ms, remaining_ms);
            return ERR_OUT_OF_RANGE;
        }
        return dominoHumanWakeupSchedule(human_ptr, behavior_ptr->progress.accounted_at_ms + remaining_ms);
    }
    if (planned_task_id != 0U) {
        clearPendingPlan(human_ptr);
        return dominoHumanWakeupSchedule(human_ptr, planned_begin_ms);
    }
    code = dominoHumanSettleNeeds(data_ptr, now_ms);
    if (code != CODE_OK) {
        return code;
    }
    if (!canPlanSleep(human_ptr, approval)) {
        clearPendingPlan(human_ptr);
        return dominoHumanWakeupCancel(human_ptr);
    }
    if (human_ptr->active_plan_request_id != 0U) {
        return dominoHumanWakeupCancel(human_ptr);
    }
    domino_runtime_ms_t trigger_ms;
    code = dominoHumanGetSleepTriggerMs(data_ptr, now_ms, &trigger_ms);
    if (code != CODE_OK) {
        return code;
    }
    if (trigger_ms > now_ms) {
        clearPendingPlan(human_ptr);
        const domino_timer_id_t old_timer_id = human_ptr->wakeup_timer_id;
        code = dominoHumanWakeupSchedule(human_ptr, trigger_ms);
        if (code == CODE_OK && human_ptr->wakeup_timer_id != old_timer_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG,
                              "sleep threshold human=%" PRIu32 " reason=energy_sufficient energy=%u due_ms=%" PRIu32, human_id,
                              (unsigned)data_ptr->energy, trigger_ms);
        }
        return code;
    }
    code = dominoHumanWakeupCancel(human_ptr);
    if (code != CODE_OK) {
        return code;
    }
    return requestSleepPlan(human_ptr, approval, now_ms);
}

/** @brief 结果只终结本次请求；版本或资格失效时保留一次重新采样意图。 */
static DOMINO_CODE applyPlanResult(DominoHumanPlanEnvelope* envelope_ptr) {
    if (g_execution.inflight_plan_count == 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "received human plan result without an inflight request");
        return ERR_INVALID_DATA;
    }
    g_execution.inflight_plan_count--;
    const DominoHumanPlanRequest* request_ptr = &envelope_ptr->request;
    /* 真实工作失败优先于身份过期；请求失效不能把后台失败改成成功。 */
    if (envelope_ptr->status == DOMINO_HUMAN_PLAN_FAILED) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "failed", planReasonName(envelope_ptr->reason), envelope_ptr->code);
        if (envelope_ptr->code >= CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "failed plan result has non-error code=%d human=%" PRIu32 " request=%" PRIu64, (int)envelope_ptr->code,
                              request_ptr->human_id, request_ptr->request_id);
            return ERR_INVALID_DATA;
        }
        return envelope_ptr->code;
    }
    if (envelope_ptr->status != DOMINO_HUMAN_PLAN_DRAFT && envelope_ptr->status != DOMINO_HUMAN_PLAN_NO_PLAN) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "unknown plan result status=%d human=%" PRIu32 " request=%" PRIu64,
                          (int)envelope_ptr->status, request_ptr->human_id, request_ptr->request_id);
        return ERR_INVALID_MESSAGE;
    }
    if (envelope_ptr->code != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "successful plan result status=%d carries error code=%d human=%" PRIu32 " request=%" PRIu64, (int)envelope_ptr->status,
                          (int)envelope_ptr->code, request_ptr->human_id, request_ptr->request_id);
        return ERR_INVALID_DATA;
    }
    DominoHuman* human_ptr = dominoGetHumanByID(request_ptr->human_id);
    if (!human_ptr || human_ptr->active_plan_request_id != request_ptr->request_id) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "stale", "request_identity", CODE_OK);
        return CODE_OK;
    }
    human_ptr->active_plan_request_id = 0U;
    if (envelope_ptr->status == DOMINO_HUMAN_PLAN_NO_PLAN) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "no_plan", planReasonName(envelope_ptr->reason), CODE_OK);
        /* 无计划是终结结果；等待实际条件事件，不能立即重复同一失败候选。 */
        clearPendingPlan(human_ptr);
        return CODE_OK;
    }
    if (envelope_ptr->task.owner_human_id != request_ptr->human_id || envelope_ptr->task.channel_index != DOMINO_HUMAN_TASK_CHANNEL_PRIMARY) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "plan draft owner/channel differs from request human=%" PRIu32 " request=%" PRIu64 " owner=%" PRIu32 " channel=%u",
                          request_ptr->human_id, request_ptr->request_id, envelope_ptr->task.owner_human_id,
                          (unsigned)envelope_ptr->task.channel_index);
        return ERR_INVALID_DATA;
    }
    const domino_building_id_t approval = getApproval(human_ptr->human_id);
    if (human_ptr->data->schedule_revision != request_ptr->schedule_revision || approval != request_ptr->approved_sleep_building_id ||
        !canPlanSleep(human_ptr, approval)) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "stale", "schedule_or_eligibility_changed", CODE_OK);
        markPlanPending(human_ptr, request_ptr->reasons);
        return CODE_OK;
    }
    domino_runtime_ms_t now_ms;
    DOMINO_CODE code = dominoTimeGetRuntimeMs(&now_ms);
    if (code != CODE_OK) {
        return code;
    }
    code = dominoHumanSettleNeeds(human_ptr->data, now_ms);
    if (code != CODE_OK) {
        return code;
    }
    domino_task_id_t task_id;
    code = dominoHumanCommitSleepTaskDraft(&envelope_ptr->task, human_ptr->data, approval, request_ptr->schedule_revision, now_ms, &task_id);
    if (code == ERR_GAME_STATE_INVALID || code == ERR_ALREADY_EXISTS || code == ERR_NOT_FOUND) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "rejected", "commit_eligibility_changed", code);
        markPlanPending(human_ptr, request_ptr->reasons);
        return CODE_OK;
    }
    if (code != CODE_OK) {
        logPlanEvent(request_ptr->human_id, request_ptr->request_id, "failed", "draft_commit", code);
        return code;
    }
    logPlanEvent(request_ptr->human_id, request_ptr->request_id, "accepted", "low_energy", CODE_OK);
    return advanceHuman(human_ptr);
}

/** @brief 业务提交成功后实际写入许可；分配失败由命令入口记录并停止执行。 */
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
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "sleep approval map capacity exhausted capacity=%u",
                                  (unsigned)capacity);
                return ERR_CAPACITY_EXCEEDED;
            }
            const khint_t new_capacity = capacity == 0U ? 4U : capacity * 2U;
            if ((size_t)new_capacity > SIZE_MAX / sizeof(HumanSleepApprovalMap_m_bucket_t)) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "sleep approval map allocation size overflows capacity=%u",
                                  (unsigned)new_capacity);
                return ERR_MEMORY_OVERFLOW;
            }
        }
    }
    if (command_ptr->type == HUMAN_EXECUTION_SUBMIT_SLEEP) {
        code = dominoHumanSubmitSleepTask(human_ptr->data, command_ptr->building_id, command_ptr->channel_index, now_ms, task_id_out_ptr);
        if (code != CODE_OK) {
            return code;
        }
    }
    if (new_approval_flag) {
        int absent_flag;
        approval_index = humanSleepApprovalMap_put(g_execution.approvals_ptr, command_ptr->human_id, &absent_flag);
        if (absent_flag < 0) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot insert sleep approval human=%" PRIu32,
                              command_ptr->human_id);
            return ERR_MEMORY_ALLOC;
        }
    }
    kh_val(g_execution.approvals_ptr, approval_index) = command_ptr->building_id;
    *human_out_ptr = human_ptr;
    return CODE_OK;
}

static void processMessage(DominoThreadMessage* message_ptr) {
    if (message_ptr->items_type == DOMINO_THREAD_MESSAGE_COMMAND && message_ptr->items_ptr &&
        message_ptr->items_size == sizeof(HumanExecutionCommand)) {
        const HumanExecutionCommand command = *(const HumanExecutionCommand*)message_ptr->items_ptr;
        checkThreadResult(mtx_lock(&g_execution.mutex));
        const bool accepted_flag =
            g_execution.command_pending_flag && command.request_id == g_execution.command_request_id && g_execution.failure_code == CODE_OK;
        checkThreadResult(mtx_unlock(&g_execution.mutex));
        domino_task_id_t task_id = 0U;
        DominoHuman* human_ptr = nullptr;
        DOMINO_CODE code = ERR_GAME_STATE_INVALID;
        bool clock_failed_flag = false;
        bool advance_failed_flag = false;
        if (accepted_flag) {
            domino_runtime_ms_t now_ms;
            code = dominoTimeGetRuntimeMs(&now_ms);
            clock_failed_flag = code != CODE_OK;
            if (code == CODE_OK) {
                code = applyCommand(&command, now_ms, &task_id, &human_ptr);
            }
        } else {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "human command is no longer pending or executor has failed human=%" PRIu32 " request=%" PRIu64, command.human_id,
                              command.request_id);
        }
        if (code == CODE_OK) {
            code = advanceHuman(human_ptr);
            advance_failed_flag = code != CODE_OK;
        }
        checkThreadResult(mtx_lock(&g_execution.mutex));
        if (code == ERR_MEMORY_ALLOC || code == ERR_MEMORY_OVERFLOW || clock_failed_flag || advance_failed_flag) {
            failExecutionLocked(code);
        }
        finishCommandLocked(command.request_id, code, task_id);
        checkThreadResult(mtx_unlock(&g_execution.mutex));
    } else if (message_ptr->items_type == DOMINO_THREAD_MESSAGE_AI_RESULT && message_ptr->items_ptr &&
               message_ptr->items_size == sizeof(DominoHumanPlanEnvelope)) {
        const DOMINO_CODE code = applyPlanResult(message_ptr->items_ptr);
        if (code != CODE_OK) {
            failExecution(code);
        }
        dominoHumanPlannerReleaseMessage(message_ptr);
        return;
    } else if (message_ptr->items_type == DOMINO_THREAD_MESSAGE_TIMER && message_ptr->items_ptr && message_ptr->items_size == sizeof(DominoTimer)) {
        DominoHuman* human_ptr;
        DOMINO_CODE code = dominoHumanWakeupConsume(message_ptr->items_ptr, &human_ptr);
        if (code == CODE_OK) {
            code = advanceHuman(human_ptr);
            if (code != CODE_OK) {
                failExecution(code);
            }
        } else if (code != ERR_NOT_FOUND) {
            failExecution(code);
        }
    } else {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "invalid human executor message type=%d payload_size=%zu",
                          (int)message_ptr->items_type, message_ptr->items_size);
        failExecution(ERR_INVALID_MESSAGE);
    }
    dominoThreadMessageRelease(message_ptr);
}

static int humanExecutionThread(void* arg_ptr) {
    (void)arg_ptr;
    size_t startup_index = 0U;
    atomic_store(&g_execution.ready_flag, true);
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(&g_execution.inbox, &epoch);
        if (code != CODE_OK) {
            return completeExecutionThread(code);
        }
        if (dominoEngineIsStopRequested() || atomic_load(&g_execution.stop_requested_flag)) {
            return completeExecutionThread(CODE_OK);
        }
        code = dominoEngineGetLifecycleError();
        if (code != CODE_OK) {
            return completeExecutionThread(code);
        }
        if (dominoTimeModuleIsFrozen()) {
            code = dominoThreadQueueWait(&g_execution.inbox, epoch, nullptr);
            if (code != CODE_OK) {
                logUnexpectedInboxStop(code, "frozen wait");
                return completeExecutionThread(code);
            }
            continue;
        }
        code = dominoHumanPlannerGetError();
        if (code != CODE_OK) {
            return completeExecutionThread(code);
        }

        /* 一轮最多处理三组各 64 项；进入后完整执行，普通冻结和退出只在下一轮观察。 */
        uint32_t consumed_count = 0U;
        for (; consumed_count < HUMAN_EXECUTION_BATCH_COUNT; consumed_count++) {
            code = dominoEngineGetLifecycleError();
            if (code != CODE_OK) {
                return completeExecutionThread(code);
            }
            DominoThreadMessage message;
            code = dominoThreadQueueConsume(&g_execution.inbox, &message, false);
            if (code == ERR_QUEUE_EMPTY) {
                break;
            }
            if (code != CODE_OK) {
                logUnexpectedInboxStop(code, "message consumption");
                return completeExecutionThread(code);
            }
            processMessage(&message);
        }
        /* 仅首次启动建立调度；暂停保留游标，普通恢复不会重新扫描已经处理的人物。 */
        uint32_t startup_count = 0U;
        for (; startup_index < kv_size(domino_all_human_list) && startup_count < HUMAN_EXECUTION_BATCH_COUNT; startup_count++) {
            DominoHuman* human_ptr = &kv_A(domino_all_human_list, startup_index++);
            code = advanceHuman(human_ptr);
            if (code != CODE_OK) {
                return completeExecutionThread(code);
            }
        }
        uint32_t pending_scanned_count = 0U;
        for (; g_execution.pending_plan_count != 0U && g_execution.inflight_plan_count < DOMINO_HUMAN_PLAN_INFLIGHT_MAX &&
               pending_scanned_count < HUMAN_EXECUTION_BATCH_COUNT;
             pending_scanned_count++) {
            code = dominoEngineGetLifecycleError();
            if (code != CODE_OK) {
                return completeExecutionThread(code);
            }
            if (g_execution.pending_plan_cursor >= kv_size(domino_all_human_list)) {
                g_execution.pending_plan_cursor = 0U;
            }
            DominoHuman* human_ptr = &kv_A(domino_all_human_list, g_execution.pending_plan_cursor++);
            if (!human_ptr->plan_submit_pending_flag) {
                continue;
            }
            code = advanceHuman(human_ptr);
            if (code != CODE_OK) {
                return completeExecutionThread(code);
            }
        }
        if (consumed_count == HUMAN_EXECUTION_BATCH_COUNT || startup_count == HUMAN_EXECUTION_BATCH_COUNT ||
            pending_scanned_count == HUMAN_EXECUTION_BATCH_COUNT) {
            continue;
        }
        code = dominoThreadQueueWait(&g_execution.inbox, epoch, nullptr);
        if (code != CODE_OK) {
            logUnexpectedInboxStop(code, "message wait");
            return completeExecutionThread(code);
        }
    }
}

DOMINO_CODE dominoHumanExecutionInit(void) {
    if (g_execution.initialized_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human executor is already initialized");
        return ERR_ALREADY_INITIALIZED;
    }
    memset(&g_execution, 0, sizeof(g_execution));
    atomic_init(&g_execution.ready_flag, false);
    atomic_init(&g_execution.stop_requested_flag, false);
    atomic_init(&g_execution.stop_completed_flag, false);
    g_execution.approvals_ptr = humanSleepApprovalMap_init();
    if (!g_execution.approvals_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot allocate human sleep approval map");
        return ERR_MEMORY_ALLOC;
    }
    if (mtx_init(&g_execution.mutex, mtx_plain) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot initialize human executor mutex");
        return ERR_MUTEX_INIT;
    }
    if (cnd_init(&g_execution.condition) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot initialize human executor command condition");
        return ERR_CONDITION_INIT;
    }
    g_execution.initialized_flag = true;
    return CODE_OK;
}

void dominoHumanExecutionExit(void) {
    if (!g_execution.initialized_flag) {
        return;
    }
    assert(!g_execution.started_flag);
    cnd_destroy(&g_execution.condition);
    mtx_destroy(&g_execution.mutex);
    humanSleepApprovalMap_destroy(g_execution.approvals_ptr);
    memset(&g_execution, 0, sizeof(g_execution));
}

DOMINO_CODE dominoHumanExecutionStart(void) {
    if (!g_execution.initialized_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot start human executor before initialization");
        return ERR_NOT_INITIALIZED;
    }
    if (g_execution.started_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human executor is already started");
        return ERR_ALREADY_INITIALIZED;
    }
    if (g_execution.failure_code != CODE_OK) {
        return g_execution.failure_code;
    }
    DOMINO_CODE code = validateFrozenWorld();
    if (code != CODE_OK) {
        return code;
    }
    code = dominoThreadQueueInit(&g_execution.inbox, sizeof(DominoThreadMessage), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                 DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
    if (code != CODE_OK) {
        return code;
    }
    code = dominoHumanWakeupInit(&g_execution.inbox);
    if (code != CODE_OK) {
        return code;
    }
    atomic_store(&g_execution.ready_flag, false);
    atomic_store(&g_execution.stop_requested_flag, false);
    atomic_store(&g_execution.stop_completed_flag, false);
    g_execution.stop_result = CODE_OK;
    g_execution.failure_code = CODE_OK;
    if (thrd_create(&g_execution.thread, humanExecutionThread, nullptr) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot create human executor thread");
        return ERR_THREAD_CREATE;
    }
    g_execution.started_flag = true;
    code = waitLifecycleFlag(&g_execution.ready_flag);
    if (code != CODE_OK) {
        return code;
    }
    /* time 仍冻结，执行线程不访问 planner；后续启动失败保留资源，由宿主结束进程。 */
    return dominoHumanPlannerStart(&g_execution.inbox);
}

void dominoHumanExecutionNotifyTimeChanged(void) {
    dominoThreadQueueWakeAll(&g_execution.inbox);
    dominoHumanPlannerNotifyTimeChanged();
}

DOMINO_CODE dominoHumanExecutionStop(void) {
    if (!g_execution.initialized_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot stop human executor before initialization");
        return ERR_NOT_INITIALIZED;
    }
    if (!g_execution.started_flag) {
        return CODE_OK;
    }
    atomic_store(&g_execution.stop_requested_flag, true);
    dominoThreadQueueWakeAll(&g_execution.inbox);
    DOMINO_CODE code = waitLifecycleFlag(&g_execution.stop_completed_flag);
    if (code != CODE_OK) {
        return code;
    }
    if (g_execution.stop_result != CODE_OK) {
        return g_execution.stop_result;
    }
    if (thrd_join(g_execution.thread, nullptr) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot join human executor thread");
        return ERR_THREAD_JOIN;
    }
    g_execution.started_flag = false;
    atomic_store(&g_execution.ready_flag, false);
    /* 执行器最后一批仍可提交规划；执行线程结束之后才关闭 planner 的入口。 */
    return dominoHumanPlannerStop();
}

DOMINO_CODE dominoHumanExecutionFinishStop(domino_runtime_ms_t now_ms) {
    assert(!g_execution.started_flag);
    DOMINO_CODE code = settleFrozenWorld(now_ms);
    if (code != CODE_OK) {
        return code;
    }
    /* planner 和 timer 均已 join，收件箱不会再收到投递。 */
    code = dominoThreadQueueStopAndWake(&g_execution.inbox, DOMINO_THREAD_QUEUE_PRODUCER);
    if (code != CODE_OK) {
        return code;
    }
    DominoThreadMessage message;
    while ((code = dominoThreadQueueConsume(&g_execution.inbox, &message, false)) == CODE_OK) {
        dominoHumanPlannerReleaseMessage(&message);
    }
    if (code != ERR_QUEUE_EMPTY && code != ERR_QUEUE_PRODUCER_STOPPED) {
        logUnexpectedInboxStop(code, "stop drain");
        return dominoEngineReportFailure(code);
    }
    code = dominoThreadQueueDestroy(&g_execution.inbox);
    if (code != CODE_OK) {
        return code;
    }
    dominoHumanWakeupExit();
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        human_ptr->active_plan_request_id = 0U;
        clearPendingPlan(human_ptr);
    }
    g_execution.pending_plan_cursor = 0U;
    g_execution.inflight_plan_count = 0U;
    return code;
}

/** @brief 公开入口已确认客户端线程和已初始化生命周期；此处只检查动态命令状态。 */
static DOMINO_CODE submitCommand(HumanExecutionCommand command, domino_task_id_t* task_id_out_ptr) {
    checkThreadResult(mtx_lock(&g_execution.mutex));
    DOMINO_CODE code;
    domino_task_id_t task_id = 0U;
    code = dominoEngineGetLifecycleError();
    if (code == CODE_OK) {
        code = g_execution.failure_code;
    }
    if (code != CODE_OK) {
        goto unlock;
    }
    if (!g_execution.started_flag) {
        domino_runtime_ms_t now_ms;
        code = dominoTimeGetRuntimeMs(&now_ms);
        if (code == CODE_OK) {
            DominoHuman* human_ptr;
            code = applyCommand(&command, now_ms, &task_id, &human_ptr);
        }
        goto unlock;
    }
    if (dominoTimeModuleIsFrozen() || dominoEngineIsStopRequested() || g_execution.command_pending_flag ||
        atomic_load(&g_execution.stop_requested_flag)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "cannot submit human command while world is frozen, stopping, or command is pending human=%" PRIu32, command.human_id);
        code = ERR_GAME_STATE_INVALID;
        goto unlock;
    }
    if (g_execution.command_request_id == UINT64_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human command request IDs exhausted");
        code = ERR_ID_EXHAUSTED;
        goto unlock;
    }
    command.request_id = ++g_execution.command_request_id;
    DominoThreadMessage message = {0};
    code = dominoThreadMessageInitCopy(&message, DOMINO_THREAD_MESSAGE_COMMAND, &command, sizeof(command));
    if (code != CODE_OK) {
        goto unlock;
    }
    g_execution.command_pending_flag = true;
    code = dominoThreadQueueProduce(&g_execution.inbox, &message);
    if (code != CODE_OK) {
        if (code == ERR_QUEUE_FULL || code == ERR_QUEUE_PRODUCER_STOPPED || code == ERR_QUEUE_CONSUMER_STOPPED) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "cannot deliver human command human=%" PRIu32 " request=%" PRIu64 " queue_code=%d", command.human_id,
                              command.request_id, (int)code);
        }
        g_execution.command_pending_flag = false;
        dominoThreadMessageRelease(&message);
        failExecutionLocked(code);
        goto unlock;
    }
    while (g_execution.command_pending_flag) {
        code = dominoEngineGetLifecycleError();
        if (code != CODE_OK) {
            failExecutionLocked(code);
            break;
        }
        code = waitCommandConditionLocked();
        if (code != CODE_OK) {
            failExecutionLocked(code);
            break;
        }
    }
    code = g_execution.command_result;
    task_id = g_execution.command_task_id;
unlock:
    const DOMINO_CODE engine_code = dominoEngineGetLifecycleError();
    if (engine_code != CODE_OK) {
        code = engine_code;
    }
    if (code == ERR_MEMORY_ALLOC || code == ERR_MEMORY_OVERFLOW) {
        failExecutionLocked(code);
    }
    if (code == CODE_OK && task_id_out_ptr) {
        *task_id_out_ptr = task_id;
    }
    checkThreadResult(mtx_unlock(&g_execution.mutex));
    return code;
}

DOMINO_CODE dominoHumanExecutionAuthorizeSleep(domino_human_id_t human_id, domino_building_id_t building_id) {
    return submitCommand((HumanExecutionCommand){.human_id = human_id, .building_id = building_id, .type = HUMAN_EXECUTION_AUTHORIZE_SLEEP}, nullptr);
}

DOMINO_CODE dominoHumanExecutionSubmitSleep(domino_human_id_t human_id, domino_building_id_t building_id, uint8_t channel_index,
                                            domino_task_id_t* task_id_out_ptr) {
    if (!task_id_out_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "sleep submission task ID output is null");
        return ERR_NULL_POINTER;
    }
    return submitCommand(
        (HumanExecutionCommand){
            .human_id = human_id, .building_id = building_id, .type = HUMAN_EXECUTION_SUBMIT_SLEEP, .channel_index = channel_index},
        task_id_out_ptr);
}
