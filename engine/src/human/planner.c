#include "planner.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "../queue/message.h"
#include "../time/entry.h"
#include "domino_shared_common.h"
#include "domino_shared_human.h"
#include "entry.h"
#include "task/sleep.h"

/** @file planner.c
 * @brief 按 FIFO 批量接收规划身份，丢弃已失效请求，持读锁查询人物数据并回传睡眠规划结果。
 * @note 请求不拥有动态资源；只有结果可以持有草案行为块，成功投递后由执行器释放。
 * Init 已准备全部资源，规划线程只在批次边界检查退出和冻结；只读共享世界，草案由执行器提交。
 */

typedef struct HumanPlanner {
    DominoThreadQueue inbox;
    DominoThreadQueue* result_queue_ptr;
    thrd_t thread;
} HumanPlanner;

static HumanPlanner g_planner;
enum {
    HUMAN_PLANNER_BATCH_COUNT = 64U,  ///< 每批处理后检查冻结和退出，不限制排队请求总数。
};

/** @brief 持读锁取得本次需要的最新值，解锁后构造草案并非阻塞回传；冻结或停止不打断已开始的批次。
 * @note 请求排队后世界可能变化，读取时复核身份；不存在或已替换的请求解锁后直接丢弃。
 * 身份匹配时仍回传草案或无草案原因，不结算世界，不跨读锁范围保留借用指针。
 * 结果消费时执行器仍须重新验证当前日程和业务条件，读锁不使草案永久有效。
 */
static void planAndSendResult(const DominoHumanPlanRequestMessage* request_ptr) {
    dominoHumanReadLock();
    const DominoHuman* human_ptr = dominoGetHumanByID(request_ptr->human_id);
    if (!human_ptr || human_ptr->active_plan_request_id != request_ptr->request_id) {
        dominoHumanReadUnlock();
        return;
    }
    // 运行期执行器在同一写锁内将人物置异常并清除请求身份，匹配的请求只属于正常人物。
    assert(human_ptr->status == DOMINO_HUMAN_STATUS_NORMAL);
    const uint8_t energy = human_ptr->data->energy;
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    dominoHumanReadUnlock();

    DominoHumanExecutionMessage message = {.type = DOMINO_HUMAN_EXECUTION_MESSAGE_PLAN_RESULT, .payload.plan = {.request = *request_ptr}};
    DominoHumanPlanResult* result_ptr = &message.payload.plan;
    DOMINO_CODE code = dominoHumanBuildSleepTaskDraft(request_ptr->human_id, energy, now_ms, &result_ptr->draft);
    switch (code) {
        case CODE_OK:
            result_ptr->outcome = DOMINO_HUMAN_PLAN_DRAFT;
            break;
        case ERR_OUT_OF_RANGE:
            // 已提交人物数据保证精力范围；剩余业务时间不足时不生成行为块。
            result_ptr->outcome = DOMINO_HUMAN_PLAN_TIME_RANGE;
            break;
        case ERR_GAME_STATE_INVALID:
            result_ptr->outcome = DOMINO_HUMAN_PLAN_STATE_CHANGED;
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
    DominoHumanPlanRequestMessage requests[HUMAN_PLANNER_BATCH_COUNT];
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
            planAndSendResult(&requests[index]);
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
    dominoThreadQueueInit(&g_planner.inbox, sizeof(DominoHumanPlanRequestMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                          DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    g_planner.result_queue_ptr = result_queue_ptr;
}

void dominoHumanPlannerStart(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_planner.thread, humanPlannerThread, nullptr), ERR_THREAD_CREATE,
                                  "cannot create human planner thread");
}

void dominoHumanPlannerNotifyTimeChanged(void) {
    dominoThreadQueueWakeAll(&g_planner.inbox);
}

uint64_t dominoHumanPlannerSubmit(domino_human_id_t human_id) {
    DominoHumanPlanRequestMessage request = {.human_id = human_id};
    const DOMINO_CODE code = dominoThreadQueueProduce(&g_planner.inbox, &request, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot submit plan request human=%" PRIu32, human_id);
    }
    return request.request_id;
}

void dominoHumanPlannerStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_planner.thread, nullptr), ERR_THREAD_JOIN, "cannot join human planner thread");
}

void dominoHumanPlannerDestroy(void) {
    // 线程已结束且业务事实已保存；未提交规划由下次 InitAfter 按人物需求重新触发，旧请求身份不跨会话。
    dominoThreadQueueDestroy(&g_planner.inbox);
    memset(&g_planner, 0, sizeof(g_planner));
}
