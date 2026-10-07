#include "persistence.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../logger/entry.h"
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

/** @brief 调用方提供有效函数、非空分段容器和日志名称，且扫描期间不修改实体和分段；存档字段仍须校验。 */
static DOMINO_CODE validatePersistedSegmentsGeneric(const void* array_ptr, size_t element_count, size_t element_size,
                                                    int (*compare_fn)(const void*, const void*), DominoNavGetU8Fn get_status,
                                                    DominoNavGetU8Fn get_region, DominoNavGetU8Fn get_network_type,
                                                    const DominoNavDataSegmentVec* segment_list, uint32_t max_segment_entity_count,
                                                    const char* entity_type) {
    if (element_count > DOMINO_NAV_ID_MAP_ENTITY_COUNT_MAX || (element_count > 0U && !array_ptr)) {
        return ERR_OUT_OF_RANGE;
    }

    const size_t segment_count = kv_size(*segment_list);
    const uint32_t alive_count = segment_count == 0U ? 0U : kv_A(*segment_list, 0U).increment_index;
    if (alive_count > element_count || segment_count > alive_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "persisted %s data_segment count does not cover active entities",
                          entity_type);
        return ERR_INVALID_DATA;
    }

    /* 先确认基础段连续覆盖 [0, alive_count)，再用同一边界校验实体状态与分组。 */
    uint32_t covered_count = 0U;
    const DominoNavDataSegment* previous_segment = nullptr;
    for (size_t segment_index = 0U; segment_index < segment_count; segment_index++) {
        const DominoNavDataSegment* segment = &kv_A(*segment_list, segment_index);
        if (segment->total_count > max_segment_entity_count) {
            return ERR_OUT_OF_RANGE;
        }
        const bool key_not_increasing =
            previous_segment &&
            (segment->region_index < previous_segment->region_index ||
             (segment->region_index == previous_segment->region_index && segment->road_network_type <= previous_segment->road_network_type));
        if (segment->init_count == 0U || segment->init_index != covered_count || segment->init_count > alive_count - covered_count ||
            segment->increment_index != alive_count || segment->total_count != segment->init_count || key_not_increasing) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                              "persisted %s data_segment has invalid range, count, increment boundary or key order at segment=%zu", entity_type,
                              segment_index);
            return ERR_INVALID_DATA;
        }

        covered_count += segment->init_count;
        previous_segment = segment;
    }
    if (covered_count != alive_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "persisted %s data_segment leaves active entities uncovered",
                          entity_type);
        return ERR_INVALID_DATA;
    }

    const void* previous_entity = nullptr;
    const DominoNavDataSegment* segment = segment_count == 0U ? nullptr : &kv_A(*segment_list, 0U);
    uint32_t segment_end = segment ? segment->init_count : 0U;
    for (uint32_t index = 0U; index < (uint32_t)element_count; index++) {
        const void* entity = (const uint8_t*)array_ptr + ((size_t)index * element_size);
        if (previous_entity && compare_fn(previous_entity, entity) > 0) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                              "persisted %s entities are not in canonical region/type/id order at index=%u", entity_type, index);
            return ERR_INVALID_DATA;
        }
        const bool active = get_status(entity) == 0U;
        if (active != (index < alive_count)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                              "persisted %s data_segment active boundary does not match entity index=%u", entity_type, index);
            return ERR_INVALID_DATA;
        }
        if (active) {
            if (index == segment_end) {
                /* 已校验的非空连续分段保证下一个活跃实体有对应分段。 */
                segment++;
                segment_end += segment->init_count;
            }
            if (get_region(entity) != segment->region_index || get_network_type(entity) != segment->road_network_type) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "persisted %s data_segment does not match entity index=%u",
                                  entity_type, index);
                return ERR_INVALID_DATA;
            }
        }
        previous_entity = entity;
    }
    return CODE_OK;
}

DOMINO_CODE navPersistenceValidateSegments(void) {
    DOMINO_CODE result = validatePersistedSegmentsGeneric(
        domino_all_fork_road_list.a, kv_size(domino_all_fork_road_list), sizeof(DominoForkRoad), compareForkRoadByCanonicalOrder, forkRoadGetStatus,
        forkRoadGetRegion, forkRoadGetNetworkType, &domino_all_fork_road_nav_data_segment_list, MAX_FORK_ROAD_COUNT_PER_NETWORK, "fork_road");
    if (result != CODE_OK) {
        return result;
    }
    return validatePersistedSegmentsGeneric(domino_all_road_list.a, kv_size(domino_all_road_list), sizeof(DominoRoad), compareRoadByCanonicalOrder,
                                            roadGetStatus, roadGetRegion, roadGetNetworkType, &domino_all_road_nav_data_segment_list, UINT32_MAX,
                                            "road");
}

void navPersistenceCompact(void) {
    rebuildCanonicalSegments(domino_all_fork_road_list.a, kv_size(domino_all_fork_road_list), sizeof(DominoForkRoad), compareForkRoadByCanonicalOrder,
                             forkRoadGetStatus, forkRoadGetRegion, forkRoadGetNetworkType, &domino_all_fork_road_nav_data_segment_list);
    rebuildCanonicalSegments(domino_all_road_list.a, kv_size(domino_all_road_list), sizeof(DominoRoad), compareRoadByCanonicalOrder, roadGetStatus,
                             roadGetRegion, roadGetNetworkType, &domino_all_road_nav_data_segment_list);
    navStateRefreshIdMapIndices();
}
