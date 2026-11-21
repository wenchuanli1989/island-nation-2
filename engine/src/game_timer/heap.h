#ifndef DOMINO_ENGINE_GAME_TIMER_HEAP_H
#define DOMINO_ENGINE_GAME_TIMER_HEAP_H

#include "entry.h"

/** @brief 数组最小堆，按 (due_ms, game_timer_id) 排序；从下标 1 开始存储，零初始化即为空堆。 */
typedef struct DominoGameTimerHeap {
    DominoGameTimerRequestMessage* requests_ptr;  ///< 有效元素为 [1, count]，堆顶在下标 1。
    uint32_t capacity;                            ///< 可存元素数，实际分配 capacity + 1 个位置。
    uint32_t count;
} DominoGameTimerHeap;

/** @brief 释放数组并恢复为空堆。 */
void dominoGameTimerHeapDestroy(DominoGameTimerHeap* heap_ptr);
/** @brief 按值插入一条请求；扩容失败致命退出。 */
void dominoGameTimerHeapPush(DominoGameTimerHeap* heap_ptr, const DominoGameTimerRequestMessage* game_timer_ptr);
/** @brief 最早到期的请求，空堆返回空指针；指针在下次堆操作前有效。 */
const DominoGameTimerRequestMessage* dominoGameTimerHeapPeek(const DominoGameTimerHeap* heap_ptr);
/** @brief 移除非空堆的堆顶。 */
void dominoGameTimerHeapPop(DominoGameTimerHeap* heap_ptr);

#endif
