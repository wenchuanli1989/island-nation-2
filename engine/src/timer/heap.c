#include "heap.h"

#include <assert.h>
#include <stdlib.h>

static inline bool isEarlier(const DominoTimer* left_ptr, const DominoTimer* right_ptr) {
    return left_ptr->due_ms < right_ptr->due_ms || (left_ptr->due_ms == right_ptr->due_ms && left_ptr->timer_id < right_ptr->timer_id);
}

static DOMINO_CODE growHeap(DominoTimerHeap* heap_ptr) {
    if (heap_ptr->capacity > UINT32_MAX / 2U) {
        return ERR_MEMORY_OVERFLOW;
    }
    uint32_t new_capacity = heap_ptr->capacity == 0U ? 128U : heap_ptr->capacity * 2U;
    // 索引 0 不存放元素，分配时额外预留一个位置。
    if ((size_t)new_capacity >= SIZE_MAX / sizeof(DominoTimer)) {
        return ERR_MEMORY_OVERFLOW;
    }
    DominoTimer* events_ptr = realloc(heap_ptr->events_ptr, ((size_t)new_capacity + 1U) * sizeof(*events_ptr));
    if (events_ptr == nullptr) {
        return ERR_MEMORY_ALLOC;
    }
    heap_ptr->events_ptr = events_ptr;
    heap_ptr->capacity = new_capacity;
    return CODE_OK;
}

void dominoTimerHeapExit(DominoTimerHeap* heap_ptr) {
    free(heap_ptr->events_ptr);
    *heap_ptr = (DominoTimerHeap){0};
}

DOMINO_CODE dominoTimerHeapPush(DominoTimerHeap* heap_ptr, const DominoTimer* timer_ptr) {
    // 扩容或上浮前复制输入，避免输入恰好引用堆内元素时指针失效。
    DominoTimer timer = *timer_ptr;
    if (heap_ptr->count == heap_ptr->capacity) {
        DOMINO_CODE code = growHeap(heap_ptr);
        if (code != CODE_OK) {
            return code;
        }
    }
    uint32_t index = ++heap_ptr->count;
    while (index > 1U) {
        uint32_t parent_index = index / 2U;
        if (!isEarlier(&timer, &heap_ptr->events_ptr[parent_index])) {
            break;
        }
        heap_ptr->events_ptr[index] = heap_ptr->events_ptr[parent_index];
        index = parent_index;
    }
    heap_ptr->events_ptr[index] = timer;
    return CODE_OK;
}

const DominoTimer* dominoTimerHeapPeek(const DominoTimerHeap* heap_ptr) {
    return heap_ptr->count == 0U ? nullptr : &heap_ptr->events_ptr[1];
}

void dominoTimerHeapPop(DominoTimerHeap* heap_ptr) {
    assert(heap_ptr->count > 0U);
    DominoTimer timer = heap_ptr->events_ptr[heap_ptr->count];
    if (--heap_ptr->count == 0U) {
        return;
    }
    uint32_t index = 1U;
    while (index <= heap_ptr->count / 2U) {
        uint32_t child_index = index * 2U;
        if (child_index + 1U <= heap_ptr->count && isEarlier(&heap_ptr->events_ptr[child_index + 1U], &heap_ptr->events_ptr[child_index])) {
            child_index++;
        }
        if (!isEarlier(&heap_ptr->events_ptr[child_index], &timer)) {
            break;
        }
        heap_ptr->events_ptr[index] = heap_ptr->events_ptr[child_index];
        index = child_index;
    }
    heap_ptr->events_ptr[index] = timer;
}
