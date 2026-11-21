#include "candidate.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "domino_shared_common.h"
#include "rules.h"

static_assert(sizeof(DominoPlanCandidateHeader) % 8U == 0U, "candidate header keeps 8-byte entry alignment");

#define CANDIDATE_ALIGN_UP8(size) (((size) + 7U) & ~(size_t)7U)

/** @brief 条目区起始偏移，紧随公共头。 */
#define CANDIDATE_ENTRY_OFFSET CANDIDATE_ALIGN_UP8(sizeof(DominoPlanCandidateHeader))

/** @brief 条目数和步骤数都取上限时的候选字节数；任何类别都可能带步骤，所以都按步骤上限计算。 */
#define CANDIDATE_MAX_BYTES(entry_type)                                                                           \
    (CANDIDATE_ALIGN_UP8(CANDIDATE_ENTRY_OFFSET + (sizeof(entry_type) * DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX)) + \
     (sizeof(DominoBehaviorPlanItem) * DOMINO_PLAN_CANDIDATE_STEP_COUNT_MAX))

// 运行期只检查条目数和步骤数；单份字节上限和大小运算不溢出在此静态保证。
static_assert(CANDIDATE_MAX_BYTES(DominoMotivationCandidateEntry) <= DOMINO_PLAN_CANDIDATE_BYTES_MAX, "motivation candidate exceeds byte limit");
static_assert(CANDIDATE_MAX_BYTES(DominoTaskCandidateEntry) <= DOMINO_PLAN_CANDIDATE_BYTES_MAX, "task candidate exceeds byte limit");
static_assert(CANDIDATE_MAX_BYTES(DominoBehaviorCandidateEntry) <= DOMINO_PLAN_CANDIDATE_BYTES_MAX, "behavior candidate exceeds byte limit");

/** @brief 按规划器类别索引的条目大小。 */
static const size_t g_entry_size_by_kind[DOMINO_HUMAN_PLANNER_COUNT] = {
    [DOMINO_HUMAN_PLANNER_MOTIVATION] = sizeof(DominoMotivationCandidateEntry),
    [DOMINO_HUMAN_PLANNER_TASK] = sizeof(DominoTaskCandidateEntry),
    [DOMINO_HUMAN_PLANNER_BEHAVIOR] = sizeof(DominoBehaviorCandidateEntry),
};

/** @brief 在途候选的总字节数：规划线程分配时预留，释放时归还。 */
static _Atomic uint64_t g_candidate_bytes_in_use;

/** @brief 在总预算内预留字节；超出返回 false，占用不变。 */
static bool reserveBudget(uint64_t bytes) {
    uint64_t current = atomic_load_explicit(&g_candidate_bytes_in_use, memory_order_relaxed);
    do {
        if (bytes > DOMINO_HUMAN_CANDIDATE_BUDGET_BYTES - current) {
            return false;
        }
    } while (
        !atomic_compare_exchange_weak_explicit(&g_candidate_bytes_in_use, &current, current + bytes, memory_order_relaxed, memory_order_relaxed));
    return true;
}

void* dominoHumanCandidateAlloc(uint8_t planner_kind, uint8_t entry_count, uint8_t required_count, uint16_t step_count) {
    assert(planner_kind < DOMINO_HUMAN_PLANNER_COUNT);
    if (entry_count > DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX || required_count > entry_count || step_count > DOMINO_PLAN_CANDIDATE_STEP_COUNT_MAX) {
        return nullptr;
    }
    const size_t step_offset = CANDIDATE_ALIGN_UP8(CANDIDATE_ENTRY_OFFSET + (g_entry_size_by_kind[planner_kind] * entry_count));
    const size_t total_size = step_offset + (sizeof(DominoBehaviorPlanItem) * step_count);
    if (!reserveBudget(total_size)) {
        return nullptr;
    }
    void* candidate_ptr = calloc(1U, total_size);
    if (!candidate_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate plan candidate bytes=%zu", total_size);
    }
    *(DominoPlanCandidateHeader*)candidate_ptr = (DominoPlanCandidateHeader){
        .total_size = (uint32_t)total_size,
        .entry_offset = (uint32_t)CANDIDATE_ENTRY_OFFSET,
        .step_offset = (uint32_t)step_offset,
        .step_count = step_count,
        .planner_kind = planner_kind,
        .entry_count = entry_count,
        .required_count = required_count,
    };
    return candidate_ptr;
}

void dominoHumanCandidateFree(void* candidate_ptr) {
    if (candidate_ptr == nullptr) {
        return;
    }
    const uint64_t total_size = dominoHumanCandidateHeader(candidate_ptr)->total_size;
    free(candidate_ptr);
    atomic_fetch_sub_explicit(&g_candidate_bytes_in_use, total_size, memory_order_relaxed);
}

bool dominoHumanCandidateIsWithinLimits(const void* candidate_ptr, uint8_t planner_kind) {
    assert(planner_kind < DOMINO_HUMAN_PLANNER_COUNT);
    const DominoPlanCandidateHeader* header_ptr = dominoHumanCandidateHeader(candidate_ptr);
    if (header_ptr->planner_kind != planner_kind || header_ptr->entry_count > DOMINO_PLAN_CANDIDATE_ENTRY_COUNT_MAX ||
        header_ptr->required_count > header_ptr->entry_count || header_ptr->step_count > DOMINO_PLAN_CANDIDATE_STEP_COUNT_MAX) {
        return false;
    }
    return (uint64_t)header_ptr->entry_offset + ((uint64_t)g_entry_size_by_kind[planner_kind] * header_ptr->entry_count) <= header_ptr->step_offset &&
           (uint64_t)header_ptr->step_offset + (sizeof(DominoBehaviorPlanItem) * (uint64_t)header_ptr->step_count) <= header_ptr->total_size;
}

uint64_t dominoHumanCandidateBytesInUse(void) {
    return atomic_load_explicit(&g_candidate_bytes_in_use, memory_order_relaxed);
}
