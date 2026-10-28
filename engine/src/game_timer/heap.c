#include "heap.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

#include "domino_shared_common.h"

static inline bool isEarlier(const DominoGameTimer* left_ptr, const DominoGameTimer* right_ptr) {
    return left_ptr->due_ms < right_ptr->due_ms || (left_ptr->due_ms == right_ptr->due_ms && left_ptr->game_timer_id < right_ptr->game_timer_id);
}

static void growHeap(DominoGameTimerHeap* heap_ptr) {
    if (heap_ptr->capacity > UINT32_MAX / 2U) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "计时器堆扩容超出元素容量范围: capacity=%" PRIu32, heap_ptr->capacity);
    }
    uint32_t new_capacity = heap_ptr->capacity == 0U ? 128U : heap_ptr->capacity * 2U;
    // 索引 0 不存放元素，分配时额外预留一个位置。
    if ((size_t)new_capacity >= SIZE_MAX / sizeof(DominoGameTimer)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "计时器堆分配大小溢出: capacity=%" PRIu32, new_capacity);
    }
    DominoGameTimer* events_ptr = realloc(heap_ptr->events_ptr, ((size_t)new_capacity + 1U) * sizeof(*events_ptr));
    if (events_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "计时器堆内存分配失败: capacity=%" PRIu32, new_capacity);
    }
    heap_ptr->events_ptr = events_ptr;
    heap_ptr->capacity = new_capacity;
}

void dominoGameTimerHeapDestroy(DominoGameTimerHeap* heap_ptr) {
    free(heap_ptr->events_ptr);
    *heap_ptr = (DominoGameTimerHeap){0};
}

void dominoGameTimerHeapPush(DominoGameTimerHeap* heap_ptr, const DominoGameTimer* game_timer_ptr) {
    // 扩容或上浮前复制输入，避免输入恰好引用堆内元素时指针失效。
    DominoGameTimer game_timer = *game_timer_ptr;
    if (heap_ptr->count == heap_ptr->capacity) {
        growHeap(heap_ptr);
    }
    uint32_t index = ++heap_ptr->count;
    while (index > 1U) {
        uint32_t parent_index = index / 2U;
        if (!isEarlier(&game_timer, &heap_ptr->events_ptr[parent_index])) {
            break;
        }
        heap_ptr->events_ptr[index] = heap_ptr->events_ptr[parent_index];
        index = parent_index;
    }
    heap_ptr->events_ptr[index] = game_timer;
}

const DominoGameTimer* dominoGameTimerHeapPeek(const DominoGameTimerHeap* heap_ptr) {
    return heap_ptr->count == 0U ? nullptr : &heap_ptr->events_ptr[1];
}

void dominoGameTimerHeapPop(DominoGameTimerHeap* heap_ptr) {
    assert(heap_ptr->count > 0U);
    DominoGameTimer game_timer = heap_ptr->events_ptr[heap_ptr->count];
    if (--heap_ptr->count == 0U) {
        return;
    }
    uint32_t index = 1U;
    while (index <= heap_ptr->count / 2U) {
        uint32_t child_index = index * 2U;
        if (child_index + 1U <= heap_ptr->count && isEarlier(&heap_ptr->events_ptr[child_index + 1U], &heap_ptr->events_ptr[child_index])) {
            child_index++;
        }
        if (!isEarlier(&heap_ptr->events_ptr[child_index], &game_timer)) {
            break;
        }
        heap_ptr->events_ptr[index] = heap_ptr->events_ptr[child_index];
        index = child_index;
    }
    heap_ptr->events_ptr[index] = game_timer;
}
