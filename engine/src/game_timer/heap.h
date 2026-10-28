#ifndef DOMINO_ENGINE_GAME_TIMER_HEAP_H
#define DOMINO_ENGINE_GAME_TIMER_HEAP_H

#include "entry.h"

/** @brief 所有目标共享的连续数组最小堆，从索引 1 存储，按到期时间、game_timer_id 排序；零初始化即可使用。 */
typedef struct DominoGameTimerHeap {
    DominoGameTimer* events_ptr;  ///< 索引 0 保留不用，堆顶位于索引 1，有效元素范围为 [1, count]。
    uint32_t capacity;            ///< 可存元素数量，实际分配 capacity + 1 个数组位置。
    uint32_t count;
} DominoGameTimerHeap;

/** @brief 释放事件数组并恢复为空堆。 */
void dominoGameTimerHeapDestroy(DominoGameTimerHeap* heap_ptr);
/** @brief 按值登记事件；容量或分配失败直接报告并终止进程。 */
void dominoGameTimerHeapPush(DominoGameTimerHeap* heap_ptr, const DominoGameTimer* game_timer_ptr);
/** @brief 返回最早到期事件，空堆返回 nullptr；返回指针仅在下次堆操作前有效。 */
const DominoGameTimer* dominoGameTimerHeapPeek(const DominoGameTimerHeap* heap_ptr);
/** @brief 移除非空堆的最早到期事件，用于取消项回收或投递完成后的移除。 */
void dominoGameTimerHeapPop(DominoGameTimerHeap* heap_ptr);

#endif
