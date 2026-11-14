#ifndef DOMINO_HUMAN_RUNTIME_H
#define DOMINO_HUMAN_RUNTIME_H
#include "../queue/message.h"
enum { DOMINO_PLAN_OPPORTUNITY_MAX = DOMINO_HUMAN_TASK_COUNT_MAX + DOMINO_HUMAN_MOTIVATION_COUNT_MAX };
/** @brief 唯一请求资格；机会队列保留先等先处理次序，重复提示不刷新次序。 */
struct DominoHumanRuntime {
    DominoPlanRequestState requests[DOMINO_PLANNER_COUNT];
    uint64_t channel_version[DOMINO_HUMAN_TASK_CHANNEL_COUNT];
    uint64_t posted_batch[DOMINO_PLANNER_COUNT];
    DominoPlanScope opportunities[DOMINO_PLANNER_COUNT][DOMINO_PLAN_OPPORTUNITY_MAX];
    uint8_t opportunity_count[DOMINO_PLANNER_COUNT];
    bool pending;
};
#endif
