#ifndef DOMINO_ENGINE_HUMAN_MESSAGE_H
#define DOMINO_ENGINE_HUMAN_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#include "task/draft.h"

/** @brief 规划工作通知直接入私有请求队列；业务数据由规划器持 human 读锁查询。 */
typedef struct DominoHumanPlanRequest {
    uint64_t request_id;  ///< 必须为首字段，由请求队列生成；结果载荷原样保留，不使用结果队列的外层 ID。
    domino_human_id_t human_id;
} DominoHumanPlanRequest;

static_assert(offsetof(DominoHumanPlanRequest, request_id) == 0, "DominoHumanPlanRequest request_id must be first for queue ID writeback");

typedef enum DominoHumanPlanOutcome {
    DOMINO_HUMAN_PLAN_DRAFT,
    DOMINO_HUMAN_PLAN_TIME_RANGE,     ///< 草案时长超过剩余可表示业务时间；当前时钟仍有效。
    DOMINO_HUMAN_PLAN_STATE_CHANGED,  ///< 请求身份仍有效，但当前精力不再满足规划条件。
} DominoHumanPlanOutcome;

/** @brief 规划结果按值内嵌于 AI_RESULT，request 保留关联身份；请求队列只保存 DominoHumanPlanRequest。
 * @note DRAFT 的 draft 独占另行分配的行为块，不引用全局行为池；其他结果的行为指针为空。
 * 结果入队成功后行为块归执行器；store 提交仅复制，执行器在消费或销毁留队结果时释放原块。
 */
typedef struct DominoHumanPlanResult {
    DominoTaskDraft draft;
    DominoHumanPlanRequest request;
    DominoHumanPlanOutcome outcome;
} DominoHumanPlanResult;

#endif
