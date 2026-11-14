#ifndef DOMINO_HUMAN_PLANNER_H
#define DOMINO_HUMAN_PLANNER_H
#include "../queue/message.h"
#include "entry.h"
#include "metrics.h"
typedef struct DominoPlannerMetrics {
    DominoTimingSamples queue_samples[DOMINO_PLAN_PRIORITY_COUNT];
    DominoTimingSamples request_samples[DOMINO_PLAN_PRIORITY_COUNT];
    DominoTimingSamples lock_samples;
    DominoTimingSamples batch_samples;
    uint64_t requests[DOMINO_PLAN_PRIORITY_COUNT];
    uint64_t stale_count;
    uint64_t batch_count;
    uint64_t queue_ns[DOMINO_PLAN_PRIORITY_COUNT];
    uint64_t request_ns[DOMINO_PLAN_PRIORITY_COUNT];
    uint64_t read_lock_ns;
    uint64_t batch_ns;
    uint32_t max_high_batch;
    uint32_t max_normal_batch;
} DominoPlannerMetrics;
/** @brief 三份独立上下文各持四队列；Init 不创建线程，Stop 只 join，Destroy 排空候选后复位。 */
void dominoHumanPlannerInit(DominoThreadQueue* wake_queue);
void dominoHumanPlannerStart(void);
void dominoHumanPlannerStop(void);
void dominoHumanPlannerDestroy(void);
void dominoHumanPlannerNotifyTimeChanged(void);
/** @brief 唯一执行器持写锁发布完整资格；非阻塞投递失败致命。 */
void dominoHumanPlannerSubmit(DominoHuman* human, DominoPlannerKind kind, DominoPlanPriority priority, DominoPlanScope scope);
bool dominoHumanPlannerConsume(DominoPlannerKind kind, DominoPlanPriority priority, DominoHumanPlanResultMessage* result);
bool dominoHumanPlannerMatches(const DominoHuman* human, DominoPlannerKind kind, DominoPlanPriority priority, uint64_t request_id);
/** @brief 仅全部线程结束后读取测量结果。 */
DominoPlannerMetrics dominoHumanPlannerMetrics(DominoPlannerKind kind);
#endif
