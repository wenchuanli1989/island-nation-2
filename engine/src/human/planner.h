#ifndef DOMINO_ENGINE_HUMAN_PLANNER_H
#define DOMINO_ENGINE_HUMAN_PLANNER_H

#include "../queue/thread_queue.h"
#include "domino_shared_behavior.h"

enum {
    DOMINO_HUMAN_PLAN_INFLIGHT_MAX = 64U,
    DOMINO_HUMAN_PLAN_TRIGGER_ENERGY = 1U,
};

/** @brief 执行线程结算后复制的必要值；不含人物、任务池、建筑或行为的借用指针。 */
typedef struct DominoHumanPlanRequest {
    uint64_t request_id;
    domino_human_id_t human_id;
    uint32_t schedule_revision;
    domino_runtime_ms_t now_ms;
    uint32_t reasons;
    domino_building_id_t current_building_id;
    domino_building_id_t approved_sleep_building_id;
    domino_task_id_t active_task_id_list[DOMINO_HUMAN_TASK_CHANNEL_COUNT];
    uint8_t energy;
    uint8_t task_count;
    bool has_nonterminal_task;
} DominoHumanPlanRequest;

typedef enum DominoHumanPlanStatus {
    DOMINO_HUMAN_PLAN_DRAFT,
    DOMINO_HUMAN_PLAN_NO_PLAN,
    DOMINO_HUMAN_PLAN_FAILED,
} DominoHumanPlanStatus;

typedef enum DominoHumanPlanReason {
    DOMINO_HUMAN_PLAN_REASON_LOW_ENERGY,
    DOMINO_HUMAN_PLAN_REASON_ENERGY_SUFFICIENT,
    DOMINO_HUMAN_PLAN_REASON_NO_AUTHORIZATION,
    DOMINO_HUMAN_PLAN_REASON_BUSY,
    DOMINO_HUMAN_PLAN_REASON_CAPACITY,
    DOMINO_HUMAN_PLAN_REASON_SYSTEM_FAILURE,
} DominoHumanPlanReason;

/** @brief AI_REQUEST 与 AI_RESULT 共用独占载荷；结果回传无需再次分配消息块。
 * @note request 始终保留原快照。DRAFT 的 task 独占行为块，提交成功后由执行线程清空 task.behaviors_ptr。
 * 收件方或发送失败方统一调用 dominoHumanPlannerReleaseMessage() 释放未接管的行为块与消息块。
 */
typedef struct DominoHumanPlanEnvelope {
    DominoTask task;
    DominoHumanPlanRequest request;
    DominoHumanPlanStatus status;
    DOMINO_CODE code;
    DominoHumanPlanReason reason;
} DominoHumanPlanEnvelope;

/** @brief 建立固定容量入口并创建规划线程，返回时已启动就绪；不读取或修改世界。
 * @pre Start 时业务时间处于冻结状态。
 * @note result_queue_ptr 是始终存活到 Stop 成功的执行收件队列，须存储 DominoThreadMessage。
 * 创建失败保留本次已创建资源；Start/Stop 由执行生命周期串行协调。
 */
DOMINO_CODE dominoHumanPlannerStart(DominoThreadQueue* result_queue_ptr);

/** @brief 时间状态或退出请求发布后唤醒批次边界等待；不等待线程回执。 */
void dominoHumanPlannerNotifyTimeChanged(void);

/** @brief 请求失败终结并唤醒执行／规划线程；不等待退出，不释放仍在使用的资源。 */
void dominoHumanPlannerRequestFailure(DOMINO_CODE code);

/** @brief 本次 Start 生命周期首个传输或工作线程故障；未 Start 时为 CODE_OK。 */
DOMINO_CODE dominoHumanPlannerGetError(void);

/** @brief 非阻塞复制并提交值快照；仅成功入队后请求成立并转移内部载荷所有权。
 * @note 执行线程限制总在途量不超过 DOMINO_HUMAN_PLAN_INFLIGHT_MAX，每人最多一个有效请求；
 * 满载或分配失败直接返回，不重试。request_id 由执行线程单调分配，不能复用消息 ID。
 * 冻结与全局退出请求不关闭入口，允许执行线程完成当前批次的提交；Stop 后拒绝提交。
 */
DOMINO_CODE dominoHumanPlannerSubmit(const DominoHumanPlanRequest* request_ptr);

/** @brief Start 成功且执行线程退出后发布停止并唤醒；join 后释放留队请求，失败保留剩余资源且不得再次 Stop。 */
DOMINO_CODE dominoHumanPlannerStop(void);

/** @brief 释放 AI envelope 中尚未转移的行为块，再释放消息；空消息可安全调用。 */
void dominoHumanPlannerReleaseMessage(DominoThreadMessage* message_ptr);

#endif
