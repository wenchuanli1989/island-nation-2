#ifndef DOMINO_ENGINE_HUMAN_MESSAGE_H
#define DOMINO_ENGINE_HUMAN_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_behavior.h"

/** @brief 执行线程结算后复制的必要值；不含人物、任务池、建筑或行为的借用指针。 */
typedef struct DominoHumanPlanRequest {
    uint64_t request_id;
    domino_human_id_t human_id;
    uint32_t schedule_revision;
    domino_runtime_ms_t now_ms;
    uint32_t reasons;
    domino_building_id_t approved_sleep_building_id;
    uint8_t energy;
} DominoHumanPlanRequest;

typedef enum DominoHumanPlanStatus {
    DOMINO_HUMAN_PLAN_DRAFT,
    DOMINO_HUMAN_PLAN_NO_PLAN,
} DominoHumanPlanStatus;

typedef enum DominoHumanPlanReason {
    DOMINO_HUMAN_PLAN_REASON_LOW_ENERGY,
    DOMINO_HUMAN_PLAN_REASON_TIME_RANGE,  ///< 草案时长超过剩余可表示业务时间；当前时钟仍有效。
} DominoHumanPlanReason;

/** @brief AI_REQUEST 与 AI_RESULT 共用的内嵌载荷；请求和结果均随消息按值复制。
 * @note request 始终保留原快照。DRAFT 的 task 独占另行分配的行为块，提交成功后由执行线程清空 task.behaviors_ptr。
 * 入队成功转移行为块所有权；收件方及正常销毁清理使用 dominoHumanPlannerReleaseMessage() 释放未接管的行为块。
 */
typedef struct DominoHumanPlanEnvelope {
    DominoTask task;
    DominoHumanPlanRequest request;
    DominoHumanPlanStatus status;
    DominoHumanPlanReason reason;
} DominoHumanPlanEnvelope;

typedef enum HumanExecutionCommandType {
    HUMAN_EXECUTION_AUTHORIZE_SLEEP,
    HUMAN_EXECUTION_SUBMIT_SLEEP,
} HumanExecutionCommandType;

/** @brief COMMAND 只携带独占值；结果通过控制条件变量返回，不借用客户端输出地址。
 * @note 执行时必须匹配唯一待处理请求；身份不匹配属于内部协议错误，直接致命退出。
 */
typedef struct HumanExecutionCommand {
    uint64_t request_id;
    domino_human_id_t human_id;
    domino_building_id_t building_id;
    HumanExecutionCommandType type;
    uint8_t channel_index;
} HumanExecutionCommand;

#endif
