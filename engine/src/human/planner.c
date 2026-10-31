#include "planner.h"

#include <inttypes.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "../queue/thread_message.h"
#include "../time/entry.h"
#include "domino_shared_common.h"
#include "domino_shared_human.h"
#include "task/sleep.h"

/** @file planner.c
 * @brief 按 FIFO 批量读取执行器的值快照，生成睡眠草案或 NO_PLAN，并回传执行器。
 * @note 请求不拥有动态资源；只有结果可以持有草案行为块，成功投递后由执行器释放。
 * Init 已准备全部资源，规划线程只在批次边界检查退出和冻结，不读取或修改世界。
 */

/** @brief 私有请求队列只传值快照；首字段由队列回写，与业务 request_id 分开。 */
typedef struct PlanRequestMessage {
    uint64_t id;
    DominoHumanPlanRequest request;
} PlanRequestMessage;

typedef struct HumanPlanner {
    DominoThreadQueue inbox;
    DominoThreadQueue* result_queue_ptr;
    thrd_t thread;
} HumanPlanner;

static HumanPlanner g_planner;
enum {
    HUMAN_PLANNER_BATCH_COUNT = 64U,  ///< 每批处理后检查冻结和退出，不限制排队请求总数。
};

/** @brief 为一份快照生成结果并非阻塞回传；冻结或停止不打断已开始的批次。
 * @pre 执行器已确认人物身份、低精力和规划资格；快照只含值，排队期间保持不变。
 */
static void planAndSendResult(const DominoHumanPlanRequest* request_ptr) {
    DominoThreadMessage message = {.type = DOMINO_THREAD_MESSAGE_AI_RESULT, .payload.plan = {.request = *request_ptr}};
    DominoHumanPlanResult* result_ptr = &message.payload.plan;
    DOMINO_CODE code = dominoHumanBuildSleepTaskDraft(request_ptr->human_id, DOMINO_HUMAN_TASK_CHANNEL_PRIMARY, request_ptr->energy,
                                                      request_ptr->now_ms, &result_ptr->draft);
    switch (code) {
        case CODE_OK:
            result_ptr->status = DOMINO_HUMAN_PLAN_DRAFT;
            result_ptr->reason = DOMINO_HUMAN_PLAN_REASON_LOW_ENERGY;
            break;
        case ERR_OUT_OF_RANGE:
            // 精力已由执行器确认；剩余业务时间不足是本次无可行计划，行为块保持为空。
            result_ptr->status = DOMINO_HUMAN_PLAN_NO_PLAN;
            result_ptr->reason = DOMINO_HUMAN_PLAN_REASON_TIME_RANGE;
            break;
        default:
            DOMINO_EXIT_WITH_ERROR(code, "cannot build sleep plan human=%" PRIu32 " request=%" PRIu64, request_ptr->human_id,
                                   request_ptr->request_id);
    }
    code = dominoThreadQueueProduce(g_planner.result_queue_ptr, &message, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot deliver plan result human=%" PRIu32 " request=%" PRIu64, request_ptr->human_id, request_ptr->request_id);
    }
    // 成功后结果及行为块归执行器；不再访问或释放已转交的草案。
}

static int humanPlannerThread(void* arg_ptr) {
    (void)arg_ptr;
    PlanRequestMessage requests[HUMAN_PLANNER_BATCH_COUNT];
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(&g_planner.inbox, &epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot snapshot human planner inbox");
        }
        if (dominoEngineIsStopping()) {
            return CODE_OK;
        }
        const uint64_t freeze_epoch = dominoTimeModuleGetFreezeEpoch();
        if (freeze_epoch != 0U) {
            dominoTimeModuleWait(freeze_epoch);
            continue;
        }
        uint32_t consumed_count;
        code = dominoThreadQueueConsumeBatch(&g_planner.inbox, requests, HUMAN_PLANNER_BATCH_COUNT, &consumed_count, false);
        if (code != CODE_OK && code != ERR_QUEUE_EMPTY) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot consume human planner requests");
        }
        for (uint32_t index = 0U; index < consumed_count; index++) {
            planAndSendResult(&requests[index].request);
        }
        if (consumed_count == HUMAN_PLANNER_BATCH_COUNT) {
            continue;
        }
        code = dominoThreadQueueWait(&g_planner.inbox, epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot wait for human planner request");
        }
    }
}

void dominoHumanPlannerInit(DominoThreadQueue* result_queue_ptr) {
    dominoThreadQueueInit(&g_planner.inbox, sizeof(PlanRequestMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    g_planner.result_queue_ptr = result_queue_ptr;
}

void dominoHumanPlannerStart(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_planner.thread, humanPlannerThread, nullptr), ERR_THREAD_CREATE,
                                  "cannot create human planner thread");
}

void dominoHumanPlannerNotifyTimeChanged(void) {
    dominoThreadQueueWakeAll(&g_planner.inbox);
}

void dominoHumanPlannerSubmit(const DominoHumanPlanRequest* request_ptr) {
    PlanRequestMessage message = {.request = *request_ptr};
    const DOMINO_CODE code = dominoThreadQueueProduce(&g_planner.inbox, &message, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot submit plan request human=%" PRIu32 " request=%" PRIu64, request_ptr->human_id, request_ptr->request_id);
    }
}

void dominoHumanPlannerStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_planner.thread, nullptr), ERR_THREAD_JOIN, "cannot join human planner thread");
}

void dominoHumanPlannerDestroy(void) {
    // 生产者和规划线程均已结束；留队请求只有值快照，可直接随队列释放。
    dominoThreadQueueDestroy(&g_planner.inbox);
    memset(&g_planner, 0, sizeof(g_planner));
}
