#ifndef DOMINO_ENGINE_HUMAN_CANDIDATE_H
#define DOMINO_ENGINE_HUMAN_CANDIDATE_H

#include <stddef.h>
#include <stdint.h>

#include "../queue/message.h"

/** @file candidate.h
 * @brief 规划候选扁平块的分配、访问与整块释放，以及候选总内存预算记账。
 * @note 规划线程按实际条目及步骤数量一次分配连续内存，构造完成后只读；入队后整块交给执行器，
 * 所有结束路径（采用、部分采用、拒绝、迟到、重复、停止留队）各整块释放一次，不按条目转交所有权。
 */

/** @brief 按实际规模分配候选块并写入公共头；条目数或步骤数超出上限、或超出候选总预算时返回空指针（暂时无方案）。
 * @note 条目大小由规划器类别决定；单份字节上限由静态断言保证，运行期不再检查。实际分配失败致命退出。返回块的条目区和步骤区已清零。
 */
void* dominoHumanCandidateAlloc(uint8_t planner_kind, uint8_t entry_count, uint8_t required_count, uint16_t step_count);

/** @brief 整块释放候选并归还预算；空指针无操作。 */
void dominoHumanCandidateFree(void* candidate_ptr);

/** @brief 公共头只读视图。 */
static inline const DominoPlanCandidateHeader* dominoHumanCandidateHeader(const void* candidate_ptr) {
    return (const DominoPlanCandidateHeader*)candidate_ptr;
}

/** @brief 条目区首地址（可写版本仅供规划线程构造期间使用）。 */
static inline void* dominoHumanCandidateEntries(void* candidate_ptr) {
    return (char*)candidate_ptr + ((const DominoPlanCandidateHeader*)candidate_ptr)->entry_offset;
}

/** @brief 步骤区首地址（可写版本仅供规划线程构造期间使用）。 */
static inline DominoBehaviorPlanItem* dominoHumanCandidateSteps(void* candidate_ptr) {
    return (DominoBehaviorPlanItem*)((char*)candidate_ptr + ((const DominoPlanCandidateHeader*)candidate_ptr)->step_offset);
}

/** @brief 执行器检查候选是否在约定规模及类别内；超出即整份拒绝。 */
bool dominoHumanCandidateWithinLimits(const void* candidate_ptr, uint8_t planner_kind);

/** @brief 当前候选预算占用字节数，供性能统计和验证。 */
uint64_t dominoHumanCandidateBytesInUse(void);

#endif
