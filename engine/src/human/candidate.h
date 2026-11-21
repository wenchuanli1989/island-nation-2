#ifndef DOMINO_ENGINE_HUMAN_CANDIDATE_H
#define DOMINO_ENGINE_HUMAN_CANDIDATE_H

#include <stddef.h>
#include <stdint.h>

#include "../queue/message.h"

/** @file candidate.h
 * @brief 规划候选扁平块：分配、访问、整块释放和全局内存预算记账。
 * @note 规划线程按实际规模一次分配，构造完只读；入队后归执行器，无论采用、拒绝、作废或停止留队，都整块释放一次。
 */

/** @brief 按实际规模分配已清零的候选块并写入公共头；超出条目、步骤上限或总预算时返回空指针（视为暂时无方案）。
 * @note 单份字节上限由静态断言保证；实际分配失败致命退出。
 */
void* dominoHumanCandidateAlloc(uint8_t planner_kind, uint8_t entry_count, uint8_t required_count, uint16_t step_count);

/** @brief 释放候选并归还预算；空指针无操作。 */
void dominoHumanCandidateFree(void* candidate_ptr);

/** @brief 公共头只读视图。 */
static inline const DominoPlanCandidateHeader* dominoHumanCandidateHeader(const void* candidate_ptr) {
    return (const DominoPlanCandidateHeader*)candidate_ptr;
}

/** @brief 条目区首地址；可写访问只用于规划线程构造期间。 */
static inline void* dominoHumanCandidateEntries(void* candidate_ptr) {
    return (char*)candidate_ptr + ((const DominoPlanCandidateHeader*)candidate_ptr)->entry_offset;
}

/** @brief 步骤区首地址；可写访问只用于规划线程构造期间。 */
static inline DominoBehaviorPlanItem* dominoHumanCandidateSteps(void* candidate_ptr) {
    return (DominoBehaviorPlanItem*)((char*)candidate_ptr + ((const DominoPlanCandidateHeader*)candidate_ptr)->step_offset);
}

/** @brief 执行器检查候选的类别、规模和区段偏移是否合法；不合法则整份拒绝。 */
bool dominoHumanCandidateIsWithinLimits(const void* candidate_ptr, uint8_t planner_kind);

/** @brief 当前在途候选占用的字节数。 */
uint64_t dominoHumanCandidateBytesInUse(void);

#endif
