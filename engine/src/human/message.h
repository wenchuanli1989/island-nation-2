#ifndef DOMINO_ENGINE_HUMAN_MESSAGE_H
#define DOMINO_ENGINE_HUMAN_MESSAGE_H

#include <stdint.h>

#include "task/draft.h"

/** @brief 执行线程结算后复制的必要值；不含人物、任务池、建筑或行为的借用指针。 */
typedef struct DominoHumanPlanRequest {
    uint64_t request_id;
    domino_human_id_t human_id;
    domino_runtime_ms_t now_ms;
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

/** @brief 规划结果按值内嵌于 AI_RESULT，request 保留输入快照；请求队列只保存 DominoHumanPlanRequest。
 * @note DRAFT 的 draft 独占另行分配的行为块，不引用全局行为池；NO_PLAN 的行为指针为空。
 * 结果入队成功后行为块归执行器；store 提交仅复制，执行器在消费或销毁留队结果时释放原块。
 */
typedef struct DominoHumanPlanResult {
    DominoTaskDraft draft;
    DominoHumanPlanRequest request;
    DominoHumanPlanStatus status;
    DominoHumanPlanReason reason;
} DominoHumanPlanResult;

#endif
