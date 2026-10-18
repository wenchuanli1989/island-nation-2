#include "planner.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "../entry.h"
#include "../logger/entry.h"
#include "../time/entry.h"
#include "domino_shared_human.h"
#include "rules.h"
#include "task/sleep.h"

typedef struct HumanPlanner {
    DominoThreadQueue inbox;
    DominoThreadQueue* result_queue_ptr;
    mtx_t mutex;
    thrd_t thread;
    DOMINO_CODE failure_code;
    DOMINO_CODE stop_result;
    atomic_bool ready_flag;
    atomic_bool stop_requested_flag;
    atomic_bool stop_completed_flag;
    bool initialized_flag;
} HumanPlanner;

static HumanPlanner g_planner;

/** @brief 规划器只允许在 Stop 时关闭生产端，其他消费端停止均破坏收件协议。 */
static void logUnexpectedConsumerStop(DOMINO_CODE code, const char* operation_ptr) {
    if (code == ERR_QUEUE_CONSUMER_STOPPED) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "human planner inbox consumer unexpectedly stopped during %s code=%d", operation_ptr, (int)code);
    }
}

/** @brief 与现有 queue/executor 一致，同步原语失效不能继续假装持锁或完成握手。 */
static void checkThreadResult(int result) {
    if (result != thrd_success) {
        (void)fprintf(stderr, "domino human planner: synchronization failure (%d)\n", result);
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
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "planner lifecycle acknowledgement wait sleep failed");
            return dominoEngineReportFailure(ERR_THREAD_SLEEP);
        }
    }
}

void dominoHumanPlannerReleaseMessage(DominoThreadMessage* message_ptr) {
    if (message_ptr && message_ptr->items_ptr && message_ptr->items_size == sizeof(DominoHumanPlanEnvelope) &&
        (message_ptr->items_type == DOMINO_THREAD_MESSAGE_AI_REQUEST || message_ptr->items_type == DOMINO_THREAD_MESSAGE_AI_RESULT)) {
        DominoHumanPlanEnvelope* envelope_ptr = message_ptr->items_ptr;
        free(envelope_ptr->task.behaviors_ptr);
        envelope_ptr->task.behaviors_ptr = nullptr;
    }
    dominoThreadMessageRelease(message_ptr);
}

void dominoHumanPlannerRequestFailure(DOMINO_CODE code) {
    code = dominoEngineReportFailure(code);
    if (!g_planner.initialized_flag) {
        return;
    }
    checkThreadResult(mtx_lock(&g_planner.mutex));
    if (g_planner.failure_code == CODE_OK) {
        g_planner.failure_code = code;
    }
    atomic_store(&g_planner.stop_requested_flag, true);
    checkThreadResult(mtx_unlock(&g_planner.mutex));
    dominoThreadQueueWakeAll(&g_planner.inbox);
    dominoThreadQueueWakeAll(g_planner.result_queue_ptr);
}

/** @brief 线程唯一退出回执；保留队列与未取载荷，正常 Stop 在 join 后释放。 */
static DOMINO_CODE completePlannerThread(DOMINO_CODE code) {
    const DOMINO_CODE engine_code = dominoEngineGetLifecycleError();
    if (engine_code != CODE_OK) {
        code = engine_code;
    }
    if (code != CODE_OK) {
        dominoHumanPlannerRequestFailure(code);
    }
    checkThreadResult(mtx_lock(&g_planner.mutex));
    if (g_planner.failure_code != CODE_OK) {
        code = g_planner.failure_code;
    }
    g_planner.stop_result = code;
    checkThreadResult(mtx_unlock(&g_planner.mutex));
    atomic_store(&g_planner.stop_completed_flag, true);
    dominoThreadQueueWakeAll(g_planner.result_queue_ptr);
    return code;
}

/** @brief 仅从独立快照选择同楼核心睡眠，不查询人物、建筑、任务池、导航或时钟。 */
static void buildPlan(DominoHumanPlanEnvelope* envelope_ptr) {
    const DominoHumanPlanRequest* request_ptr = &envelope_ptr->request;
    envelope_ptr->status = DOMINO_HUMAN_PLAN_NO_PLAN;
    envelope_ptr->code = CODE_OK;
    if (request_ptr->energy > DOMINO_HUMAN_NEED_MAX || request_ptr->task_count > DOMINO_HUMAN_TASK_COUNT_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "invalid plan snapshot human=%" PRIu32 " request=%" PRIu64 " energy=%u task_count=%u", request_ptr->human_id,
                          request_ptr->request_id, (unsigned)request_ptr->energy, (unsigned)request_ptr->task_count);
        envelope_ptr->status = DOMINO_HUMAN_PLAN_FAILED;
        envelope_ptr->code = ERR_INVALID_DATA;
        envelope_ptr->reason = DOMINO_HUMAN_PLAN_REASON_SYSTEM_FAILURE;
        return;
    }
    if (request_ptr->approved_sleep_building_id == 0U || request_ptr->current_building_id != request_ptr->approved_sleep_building_id) {
        envelope_ptr->reason = DOMINO_HUMAN_PLAN_REASON_NO_AUTHORIZATION;
        return;
    }
    bool busy_flag = request_ptr->has_nonterminal_task;
    for (uint8_t channel = 0U; channel < DOMINO_HUMAN_TASK_CHANNEL_COUNT; channel++) {
        busy_flag |= request_ptr->active_task_id_list[channel] != 0U;
    }
    if (busy_flag) {
        envelope_ptr->reason = DOMINO_HUMAN_PLAN_REASON_BUSY;
        return;
    }
    if (request_ptr->task_count == DOMINO_HUMAN_TASK_COUNT_MAX) {
        envelope_ptr->reason = DOMINO_HUMAN_PLAN_REASON_CAPACITY;
        return;
    }
    if (request_ptr->energy > DOMINO_HUMAN_SLEEP_TRIGGER_ENERGY) {
        envelope_ptr->reason = DOMINO_HUMAN_PLAN_REASON_ENERGY_SUFFICIENT;
        return;
    }
    envelope_ptr->code =
        dominoHumanBuildSleepTaskDraft(request_ptr->human_id, request_ptr->approved_sleep_building_id, DOMINO_HUMAN_TASK_CHANNEL_PRIMARY,
                                       request_ptr->energy, request_ptr->now_ms, &envelope_ptr->task);
    envelope_ptr->status = envelope_ptr->code == CODE_OK ? DOMINO_HUMAN_PLAN_DRAFT : DOMINO_HUMAN_PLAN_FAILED;
    envelope_ptr->reason = envelope_ptr->code == CODE_OK ? DOMINO_HUMAN_PLAN_REASON_LOW_ENERGY : DOMINO_HUMAN_PLAN_REASON_SYSTEM_FAILURE;
}

/** @brief 当前批次的请求完成计算和非阻塞回传；冻结不打断已开始的批次。
 * @pre 消息成功取自私有 inbox，唯一生产入口 Submit 已构造完整的 AI_REQUEST envelope。
 */
static DOMINO_CODE processRequest(DominoThreadMessage* message_ptr) {
    DOMINO_CODE code = dominoEngineGetLifecycleError();
    if (code == CODE_OK) {
        DominoHumanPlanEnvelope* envelope_ptr = message_ptr->items_ptr;
        buildPlan(envelope_ptr);
        if (envelope_ptr->status == DOMINO_HUMAN_PLAN_FAILED) {
            code = envelope_ptr->code;
        } else {
            message_ptr->items_type = DOMINO_THREAD_MESSAGE_AI_RESULT;
            code = dominoThreadQueueProduce(g_planner.result_queue_ptr, message_ptr);
            if (code == ERR_QUEUE_FULL || code == ERR_QUEUE_PRODUCER_STOPPED || code == ERR_QUEUE_CONSUMER_STOPPED) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                                  "cannot deliver plan result human=%" PRIu32 " request=%" PRIu64 " queue_code=%d", envelope_ptr->request.human_id,
                                  envelope_ptr->request.request_id, (int)code);
            }
        }
    }
    if (code != CODE_OK) {
        dominoHumanPlannerReleaseMessage(message_ptr);
    }
    return code;
}

static int humanPlannerThread(void* arg_ptr) {
    (void)arg_ptr;
    atomic_store(&g_planner.ready_flag, true);
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(&g_planner.inbox, &epoch);
        if (code != CODE_OK) {
            return completePlannerThread(code);
        }
        if (dominoEngineIsStopRequested() || atomic_load(&g_planner.stop_requested_flag)) {
            return completePlannerThread(CODE_OK);
        }
        code = dominoEngineGetLifecycleError();
        if (code != CODE_OK) {
            return completePlannerThread(code);
        }
        if (!dominoTimeModuleIsFrozen()) {
            uint32_t consumed_count = 0U;
            for (; consumed_count < DOMINO_HUMAN_PLAN_INFLIGHT_MAX; consumed_count++) {
                DominoThreadMessage message;
                code = dominoThreadQueueConsume(&g_planner.inbox, &message, false);
                if (code == ERR_QUEUE_EMPTY || code == ERR_QUEUE_PRODUCER_STOPPED) {
                    break;
                }
                if (code != CODE_OK) {
                    logUnexpectedConsumerStop(code, "request consumption");
                    return completePlannerThread(code);
                }
                code = processRequest(&message);
                if (code != CODE_OK) {
                    return completePlannerThread(code);
                }
            }
            if (consumed_count == DOMINO_HUMAN_PLAN_INFLIGHT_MAX) {
                continue;
            }
        }
        code = dominoThreadQueueWait(&g_planner.inbox, epoch, nullptr);
        if (code != CODE_OK && code != ERR_QUEUE_PRODUCER_STOPPED) {
            logUnexpectedConsumerStop(code, "request wait");
            return completePlannerThread(code);
        }
    }
}

DOMINO_CODE dominoHumanPlannerStart(DominoThreadQueue* result_queue_ptr) {
    if (g_planner.initialized_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human planner is already initialized");
        return ERR_ALREADY_INITIALIZED;
    }
    if (!result_queue_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human planner result queue is null");
        return ERR_NULL_POINTER;
    }
    if (!result_queue_ptr->initialized) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human planner result queue is not initialized");
        return ERR_NOT_INITIALIZED;
    }
    if (result_queue_ptr->item_size != sizeof(DominoThreadMessage)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human planner result queue item size=%zu expected=%zu",
                          result_queue_ptr->item_size, sizeof(DominoThreadMessage));
        return ERR_INVALID_PARAM;
    }
    memset(&g_planner, 0, sizeof(g_planner));
    atomic_init(&g_planner.ready_flag, false);
    atomic_init(&g_planner.stop_requested_flag, false);
    atomic_init(&g_planner.stop_completed_flag, false);
    g_planner.stop_result = CODE_OK;
    if (mtx_init(&g_planner.mutex, mtx_plain) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot initialize human planner mutex");
        return ERR_MUTEX_INIT;
    }
    DOMINO_CODE code =
        dominoThreadQueueInit(&g_planner.inbox, sizeof(DominoThreadMessage), DOMINO_HUMAN_PLAN_INFLIGHT_MAX, DOMINO_HUMAN_PLAN_INFLIGHT_MAX);
    if (code != CODE_OK) {
        return code;
    }
    g_planner.result_queue_ptr = result_queue_ptr;
    g_planner.initialized_flag = true;
    if (thrd_create(&g_planner.thread, humanPlannerThread, nullptr) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot create human planner thread");
        return ERR_THREAD_CREATE;
    }
    return waitLifecycleFlag(&g_planner.ready_flag);
}

void dominoHumanPlannerNotifyTimeChanged(void) {
    if (!g_planner.initialized_flag) {
        return;
    }
    dominoThreadQueueWakeAll(&g_planner.inbox);
}

DOMINO_CODE dominoHumanPlannerGetError(void) {
    if (!g_planner.initialized_flag) {
        return CODE_OK;
    }
    checkThreadResult(mtx_lock(&g_planner.mutex));
    const DOMINO_CODE code = g_planner.failure_code;
    checkThreadResult(mtx_unlock(&g_planner.mutex));
    return code;
}

DOMINO_CODE dominoHumanPlannerSubmit(const DominoHumanPlanRequest* request_ptr) {
    if (!request_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human planner request is null");
        return ERR_NULL_POINTER;
    }
    if (!g_planner.initialized_flag) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot submit human plan before planner initialization");
        return ERR_NOT_INITIALIZED;
    }
    if (request_ptr->request_id == 0U || request_ptr->human_id == 0U || request_ptr->reasons == 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "invalid plan request human=%" PRIu32 " request=%" PRIu64 " reasons=%" PRIu32, request_ptr->human_id,
                          request_ptr->request_id, request_ptr->reasons);
        return ERR_INVALID_PARAM;
    }
    checkThreadResult(mtx_lock(&g_planner.mutex));
    DOMINO_CODE code = dominoEngineGetLifecycleError();
    if (code == CODE_OK) {
        code = g_planner.failure_code;
    }
    if (code == CODE_OK && atomic_load(&g_planner.stop_requested_flag)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                          "cannot submit plan after planner stop requested human=%" PRIu32 " request=%" PRIu64, request_ptr->human_id,
                          request_ptr->request_id);
        code = ERR_GAME_STATE_INVALID;
    }
    if (code != CODE_OK) {
        goto unlock;
    }
    const DominoHumanPlanEnvelope envelope = {.request = *request_ptr};
    DominoThreadMessage message = {0};
    code = dominoThreadMessageInitCopy(&message, DOMINO_THREAD_MESSAGE_AI_REQUEST, &envelope, sizeof(envelope));
    if (code != CODE_OK) {
        goto unlock;
    }
    code = dominoThreadQueueProduce(&g_planner.inbox, &message);
    if (code != CODE_OK) {
        if (code == ERR_QUEUE_FULL || code == ERR_QUEUE_PRODUCER_STOPPED || code == ERR_QUEUE_CONSUMER_STOPPED) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "cannot submit plan request human=%" PRIu32 " request=%" PRIu64 " queue_code=%d", request_ptr->human_id,
                              request_ptr->request_id, (int)code);
        }
        dominoHumanPlannerReleaseMessage(&message);
    }
unlock:
    checkThreadResult(mtx_unlock(&g_planner.mutex));
    return code;
}

DOMINO_CODE dominoHumanPlannerStop(void) {
    if (!g_planner.initialized_flag) {
        return CODE_OK;
    }
    atomic_store(&g_planner.stop_requested_flag, true);
    DOMINO_CODE code = dominoThreadQueueStopAndWake(&g_planner.inbox, DOMINO_THREAD_QUEUE_PRODUCER);
    if (code != CODE_OK) {
        return code;
    }
    code = waitLifecycleFlag(&g_planner.stop_completed_flag);
    if (code != CODE_OK) {
        return code;
    }
    if (g_planner.stop_result != CODE_OK) {
        return g_planner.stop_result;
    }
    if (thrd_join(g_planner.thread, nullptr) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot join human planner thread");
        return ERR_THREAD_JOIN;
    }
    DominoThreadMessage message;
    while ((code = dominoThreadQueueConsume(&g_planner.inbox, &message, false)) == CODE_OK) {
        dominoHumanPlannerReleaseMessage(&message);
    }
    if (code != ERR_QUEUE_EMPTY && code != ERR_QUEUE_PRODUCER_STOPPED) {
        logUnexpectedConsumerStop(code, "stop drain");
        return dominoEngineReportFailure(code);
    }
    code = dominoThreadQueueDestroy(&g_planner.inbox);
    if (code != CODE_OK) {
        return code;
    }
    mtx_destroy(&g_planner.mutex);
    memset(&g_planner, 0, sizeof(g_planner));
    return CODE_OK;
}
