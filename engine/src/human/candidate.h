#ifndef DOMINO_HUMAN_CANDIDATE_H
#define DOMINO_HUMAN_CANDIDATE_H
#include "../queue/message.h"
#include "entry.h"
/** @brief 同一写锁内有界预检后提交，不接管载荷；拒绝不改变已接受安排。 */
bool dominoHumanAcceptCandidate(DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope, const DominoPlanCandidate* candidate,
                                domino_runtime_ms_t now);
/** @brief 规划计算入口；调用者持读锁，返回实际规模的自有扁平块，不收发消息。 */
DominoPlanCandidate* dominoHumanPlanMotivations(const DominoHuman* human, DominoPlanScope scope, domino_runtime_ms_t now, uint32_t budget,
                                                DominoPlanOutcome* outcome);
DominoPlanCandidate* dominoHumanPlanTasks(const DominoHuman* human, DominoPlanScope scope, domino_runtime_ms_t now, uint32_t budget,
                                          DominoPlanOutcome* outcome);
DominoPlanCandidate* dominoHumanPlanBehaviors(const DominoHuman* human, DominoPlanScope scope, domino_runtime_ms_t now, uint32_t budget,
                                              DominoPlanOutcome* outcome);
DominoPlanPriority dominoHumanPlanningPriority(const DominoHuman* human, DominoPlannerKind kind, DominoPlanScope scope);
void* dominoHumanAllocateCandidate(size_t bytes, uint16_t items, uint16_t steps);
/** @brief 整块释放；包括拒绝、重复、超时和停止留队路径。 */
void dominoHumanFreeCandidate(DominoPlanCandidate* candidate);
size_t dominoHumanCandidateLiveBytes(void);
size_t dominoHumanCandidatePeakBytes(void);
typedef struct DominoCandidateMetrics {
    uint64_t allocations, allocated_bytes, allocation_ns;
} DominoCandidateMetrics;
/** @brief 仅所有生产线程结束后读取，Destroy 复位。 */
DominoCandidateMetrics dominoHumanCandidateMetrics(void);
void dominoHumanCandidateResetMetrics(void);
#endif
