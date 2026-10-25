#include "planner.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "../entry.h"
#include "../time/entry.h"
#include "domino_shared_common.h"
#include "domino_shared_human.h"
#include "rules.h"
#include "task/sleep.h"

typedef struct HumanPlanner {
    DominoThreadQueue inbox;
    DominoThreadQueue* result_queue_ptr;
    thrd_t thread;
    atomic_bool ready_flag;  ///< 仅用于首次启动握手，由静态初值或 Destroy 复位。
} HumanPlanner;

static HumanPlanner g_planner;

/** @brief 等待工作线程发布就绪，保证 Start 返回时已完成启动握手。 */
static void waitUntilReady(void) {
    const struct timespec interval = {.tv_nsec = 10000000L};
    while (!atomic_load(&g_planner.ready_flag)) {
        if (thrd_sleep(&interval, nullptr) == -2) {
            DOMINO_EXIT_WITH_ERROR(ERR_THREAD_SLEEP, "planner startup acknowledgement wait sleep failed");
        }
    }
}

void dominoHumanPlannerReleaseMessage(DominoThreadMessage* message_ptr) {
    if (message_ptr && (message_ptr->type == DOMINO_THREAD_MESSAGE_AI_REQUEST || message_ptr->type == DOMINO_THREAD_MESSAGE_AI_RESULT)) {
        free(message_ptr->payload.plan.task.behaviors_ptr);
        *message_ptr = (DominoThreadMessage){0};
    }
}

/** @brief 仅从独立快照选择同楼核心睡眠，不查询人物、建筑、任务池、导航或时钟。 */
static void buildPlan(DominoHumanPlanEnvelope* envelope_ptr) {
    const DominoHumanPlanRequest* request_ptr = &envelope_ptr->request;
    envelope_ptr->status = DOMINO_HUMAN_PLAN_NO_PLAN;
    if (request_ptr->energy > DOMINO_HUMAN_NEED_MAX || request_ptr->task_count > DOMINO_HUMAN_TASK_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "invalid plan snapshot human=%" PRIu32 " request=%" PRIu64 " energy=%u task_count=%u",
                               request_ptr->human_id, request_ptr->request_id, (unsigned)request_ptr->energy, (unsigned)request_ptr->task_count);
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
    const DOMINO_CODE code =
        dominoHumanBuildSleepTaskDraft(request_ptr->human_id, request_ptr->approved_sleep_building_id, DOMINO_HUMAN_TASK_CHANNEL_PRIMARY,
                                       request_ptr->energy, request_ptr->now_ms, &envelope_ptr->task);
    if (code == ERR_OUT_OF_RANGE) {
        // 快照已验证；剩余可表示时长不足属于本次计划无法成立，不代表当前时钟失效。
        envelope_ptr->reason = DOMINO_HUMAN_PLAN_REASON_TIME_RANGE;
        return;
    }
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot build sleep plan human=%" PRIu32 " request=%" PRIu64, request_ptr->human_id, request_ptr->request_id);
    }
    envelope_ptr->status = DOMINO_HUMAN_PLAN_DRAFT;
    envelope_ptr->reason = DOMINO_HUMAN_PLAN_REASON_LOW_ENERGY;
}

/** @brief 当前批次的请求完成计算和非阻塞回传；冻结不打断已开始的批次。
 * @pre 消息成功取自私有 inbox，唯一生产入口 Submit 已构造完整的 AI_REQUEST envelope。
 */
static void processRequest(DominoThreadMessage* message_ptr) {
    DominoHumanPlanEnvelope* envelope_ptr = &message_ptr->payload.plan;
    buildPlan(envelope_ptr);
    message_ptr->type = DOMINO_THREAD_MESSAGE_AI_RESULT;
    const DOMINO_CODE code = dominoThreadQueueProduce(g_planner.result_queue_ptr, message_ptr);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot deliver plan result human=%" PRIu32 " request=%" PRIu64, envelope_ptr->request.human_id,
                               envelope_ptr->request.request_id);
    }
    // 成功后行为块归结果队列的接收者；当前消息值直接丢弃，不再访问或释放行为指针。
}

static int humanPlannerThread(void* arg_ptr) {
    (void)arg_ptr;
    atomic_store(&g_planner.ready_flag, true);
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(&g_planner.inbox, &epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot snapshot human planner inbox");
        }
        if (dominoEngineIsStopping()) {
            return CODE_OK;
        }
        if (!dominoTimeModuleIsFrozen()) {
            uint32_t consumed_count = 0U;
            for (; consumed_count < DOMINO_HUMAN_PLAN_INFLIGHT_MAX; consumed_count++) {
                DominoThreadMessage message;
                code = dominoThreadQueueConsume(&g_planner.inbox, &message, false);
                if (code == ERR_QUEUE_EMPTY) {
                    break;
                }
                if (code != CODE_OK) {
                    DOMINO_EXIT_WITH_ERROR(code, "cannot consume human planner request");
                }
                processRequest(&message);
            }
            if (consumed_count == DOMINO_HUMAN_PLAN_INFLIGHT_MAX) {
                continue;
            }
        }
        code = dominoThreadQueueWait(&g_planner.inbox, epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot wait for human planner request");
        }
    }
}

void dominoHumanPlannerInit(DominoThreadQueue* result_queue_ptr) {
    dominoThreadQueueInit(&g_planner.inbox, sizeof(DominoThreadMessage), DOMINO_HUMAN_PLAN_INFLIGHT_MAX, DOMINO_HUMAN_PLAN_INFLIGHT_MAX);
    g_planner.result_queue_ptr = result_queue_ptr;
}

void dominoHumanPlannerStart(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_planner.thread, humanPlannerThread, nullptr), ERR_THREAD_CREATE,
                                  "cannot create human planner thread");
    waitUntilReady();
}

void dominoHumanPlannerNotifyTimeChanged(void) {
    dominoThreadQueueWakeAll(&g_planner.inbox);
}

void dominoHumanPlannerSubmit(const DominoHumanPlanRequest* request_ptr) {
    if (!request_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "human planner request is null");
    }
    if (request_ptr->request_id == 0U || request_ptr->human_id == 0U || request_ptr->reasons == 0U) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "invalid plan request human=%" PRIu32 " request=%" PRIu64 " reasons=%" PRIu32,
                               request_ptr->human_id, request_ptr->request_id, request_ptr->reasons);
    }
    DominoThreadMessage message = {.type = DOMINO_THREAD_MESSAGE_AI_REQUEST, .payload.plan = {.request = *request_ptr}};
    const DOMINO_CODE code = dominoThreadQueueProduce(&g_planner.inbox, &message);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot submit plan request human=%" PRIu32 " request=%" PRIu64, request_ptr->human_id, request_ptr->request_id);
    }
}

void dominoHumanPlannerStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_planner.thread, nullptr), ERR_THREAD_JOIN, "cannot join human planner thread");
}

void dominoHumanPlannerDestroy(void) {
    dominoThreadQueueBeginFlush(&g_planner.inbox);
    DominoThreadMessage message;
    DOMINO_CODE code;
    while ((code = dominoThreadQueueConsume(&g_planner.inbox, &message, false)) == CODE_OK) {
        dominoHumanPlannerReleaseMessage(&message);
    }
    if (code != ERR_QUEUE_FLUSHING) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot drain human planner inbox during destroy");
    }
    dominoThreadQueueDestroy(&g_planner.inbox);
    memset(&g_planner, 0, sizeof(g_planner));
    atomic_init(&g_planner.ready_flag, false);
}
