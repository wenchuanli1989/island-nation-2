#include "persistence.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "model_view.h"
#include "state.h"
#include "storage_view.h"

static int compareForkRoadByCanonicalOrder(const void* left_ptr, const void* right_ptr) {
    const DominoForkRoad* left = (const DominoForkRoad*)left_ptr;
    const DominoForkRoad* right = (const DominoForkRoad*)right_ptr;
    if (left->status != 0U && right->status == 0U) {
        return 1;
    }
    if (left->status == 0U && right->status != 0U) {
        return -1;
    }
    if (left->region_index != right->region_index) {
        return left->region_index < right->region_index ? -1 : 1;
    }
    if (left->type != right->type) {
        return left->type < right->type ? -1 : 1;
    }
    return (left->id > right->id) - (left->id < right->id);
}

static int compareRoadByCanonicalOrder(const void* left_ptr, const void* right_ptr) {
    const DominoRoad* left = (const DominoRoad*)left_ptr;
    const DominoRoad* right = (const DominoRoad*)right_ptr;
    if (left->status != 0U && right->status == 0U) {
        return 1;
    }
    if (left->status == 0U && right->status != 0U) {
        return -1;
    }
    if (left->region_index != right->region_index) {
        return left->region_index < right->region_index ? -1 : 1;
    }
    if (left->type != right->type) {
        return left->type < right->type ? -1 : 1;
    }
    return (left->id > right->id) - (left->id < right->id);
}

static uint8_t forkRoadGetStatus(const void* element) {
    return ((const DominoForkRoad*)element)->status;
}

static uint8_t forkRoadGetRegion(const void* element) {
    return ((const DominoForkRoad*)element)->region_index;
}

static uint8_t forkRoadGetNetworkType(const void* element) {
    return ((const DominoForkRoad*)element)->type;
}

static uint8_t roadGetStatus(const void* element) {
    return ((const DominoRoad*)element)->status;
}

static uint8_t roadGetRegion(const void* element) {
    return ((const DominoRoad*)element)->region_index;
}

static uint8_t roadGetNetworkType(const void* element) {
    return ((const DominoRoad*)element)->type;
}

/** @brief 调用方保证实体数可由 uint32_t 表示、字段读取函数有效，且没有借用实体地址的缓存。 */
static void rebuildCanonicalSegments(void* array_ptr, size_t element_count, size_t element_size, int (*compare_fn)(const void*, const void*),
                                     DominoNavGetU8Fn get_status, DominoNavGetU8Fn get_region, DominoNavGetU8Fn get_network_type,
                                     DominoNavDataSegmentVec* out_segment_list) {
    out_segment_list->n = 0U;
    if (element_count > 1U) {
        qsort(array_ptr, element_count, element_size, compare_fn);
    }

    uint32_t alive_count = 0U;
    DominoNavDataSegment* current_segment = nullptr;
    for (; alive_count < (uint32_t)element_count; alive_count++) {
        const void* element = (const uint8_t*)array_ptr + ((size_t)alive_count * element_size);
        if (get_status(element) != 0U) {
            break;
        }
        const uint8_t region_index = get_region(element);
        const uint8_t network_type = get_network_type(element);
        if (!current_segment || current_segment->region_index != region_index || current_segment->road_network_type != network_type) {
            kv_push(DominoNavDataSegment, *out_segment_list,
                    ((DominoNavDataSegment){
                        .init_index = alive_count,
                        .region_index = region_index,
                        .road_network_type = network_type,
                    }));
            /* kv_push 可能扩容；此前保存的 segment 指针不能继续使用。 */
            current_segment = &kv_A(*out_segment_list, kv_size(*out_segment_list) - 1U);
        }
        current_segment->init_count++;
    }

    for (size_t segment_index = 0U; segment_index < kv_size(*out_segment_list); segment_index++) {
        DominoNavDataSegment* segment = &kv_A(*out_segment_list, segment_index);
        segment->increment_index = alive_count;
        segment->total_count = segment->init_count;
    }
}

void navPersistenceCompact(void) {
    rebuildCanonicalSegments(domino_all_fork_road_list.a, kv_size(domino_all_fork_road_list), sizeof(DominoForkRoad), compareForkRoadByCanonicalOrder,
                             forkRoadGetStatus, forkRoadGetRegion, forkRoadGetNetworkType, &domino_all_fork_road_nav_data_segment_list);
    rebuildCanonicalSegments(domino_all_road_list.a, kv_size(domino_all_road_list), sizeof(DominoRoad), compareRoadByCanonicalOrder, roadGetStatus,
                             roadGetRegion, roadGetNetworkType, &domino_all_road_nav_data_segment_list);
    navStateRefreshIdMapIndices();
}
