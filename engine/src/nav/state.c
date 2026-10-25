#include "state.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../host/entry.h"
#include "domino_shared_common.h"
#include "graph.h"
#include "klib/kvec.h"
#include "road_id.h"
#include "storage_view.h"

DominoForkRoadVec domino_all_fork_road_list;
DominoRoadVec domino_all_road_list;
DominoRoadLineVec domino_all_road_line_list;
DominoNavDataSegmentVec domino_all_fork_road_nav_data_segment_list;
DominoNavDataSegmentVec domino_all_road_nav_data_segment_list;

DominoForkRoadIDMap* dominoForkRoadIdMap;
DominoRoadIDMap* dominoRoadIdMap;
DominoRoadLineIDMap* dominoRoadLineIdMap;

static bool g_nav_compaction_required = true;

static DominoNavPathCache* g_nav_path_cache_slots[DOMINO_NAV_PATH_CACHE_SLOT_COUNT];

static inline size_t navStatePathCacheSlotIndex(uint8_t region_index, uint8_t road_network_type) {
    return ((size_t)region_index << 8U) | (size_t)road_network_type;
}

DominoNavPathCache* navStatePreparePathCache(uint8_t region_index, uint8_t road_network_type) {
    const size_t slot_index = navStatePathCacheSlotIndex(region_index, road_network_type);
    DominoNavPathCache* cache = g_nav_path_cache_slots[slot_index];
    if (!cache) {
        cache = (DominoNavPathCache*)calloc(1U, sizeof(*cache));
        if (!cache) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate path cache: region=%u network=%u", (unsigned)region_index,
                                   (unsigned)road_network_type);
        }
        g_nav_path_cache_slots[slot_index] = cache;
    }
    return cache;
}

/** @brief 清空有效缓存槽；调用方保证 cache 非空。 */
static void navStateResetPathCache(DominoNavPathCache* cache) {
    free(cache->path_matrix);
    free(cache->fork_road_list);
    cache->path_matrix = nullptr;
    cache->fork_road_list = nullptr;
    cache->vertex_count = 0U;
}

static void navStateDestroyPathCaches(void) {
    for (size_t slot_index = 0U; slot_index < DOMINO_NAV_PATH_CACHE_SLOT_COUNT; slot_index++) {
        DominoNavPathCache* cache = g_nav_path_cache_slots[slot_index];
        if (!cache) {
            continue;
        }
        navStateResetPathCache(cache);
        free(cache);
        g_nav_path_cache_slots[slot_index] = nullptr;
    }
}

static void advanceNextId(_Atomic uint32_t* next_id_ptr, uint32_t max_id) {
    if (max_id == 0U) {
        return;
    }
    uint32_t observed = atomic_load_explicit(next_id_ptr, memory_order_relaxed);
    const uint32_t required_next_id = max_id + 1U;
    while (observed < required_next_id &&
           !atomic_compare_exchange_weak_explicit(next_id_ptr, &observed, required_next_id, memory_order_relaxed, memory_order_relaxed)) {
    }
}

void navStateInit(void) {
    dominoForkRoadIdMap = dominoForkRoadIdMap_init();
    if (!dominoForkRoadIdMap) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate fork road ID map");
    }
    dominoRoadIdMap = dominoRoadIdMap_init();
    if (!dominoRoadIdMap) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate road ID map");
    }
    dominoRoadLineIdMap = dominoRoadLineIdMap_init();
    if (!dominoRoadLineIdMap) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate road line ID map");
    }
}

void navStateDestroy(void) {
    navStateDestroyPathCaches();

    for (size_t index = 0U; index < kv_size(domino_all_road_list); index++) {
        DominoRoad* road = &kv_A(domino_all_road_list, index);
        if (road->corner_point_count > MIN_CORNER_POINTS_PER_ROAD) {
            free(road->corner_point_ptr);
        }
    }

    dominoForkRoadIdMap_destroy(dominoForkRoadIdMap);
    dominoRoadIdMap_destroy(dominoRoadIdMap);
    dominoRoadLineIdMap_destroy(dominoRoadLineIdMap);
    dominoForkRoadIdMap = nullptr;
    dominoRoadIdMap = nullptr;
    dominoRoadLineIdMap = nullptr;

    kv_destroy(domino_all_fork_road_list);
    kv_destroy(domino_all_road_list);
    kv_destroy(domino_all_road_line_list);
    kv_destroy(domino_all_fork_road_nav_data_segment_list);
    kv_destroy(domino_all_road_nav_data_segment_list);
    kv_init(domino_all_fork_road_list);
    kv_init(domino_all_road_list);
    kv_init(domino_all_road_line_list);
    kv_init(domino_all_fork_road_nav_data_segment_list);
    kv_init(domino_all_road_nav_data_segment_list);

    g_nav_compaction_required = true;
}

bool navStateIsReady(void) {
    return dominoForkRoadIdMap != nullptr;
}

bool navStateNeedsCompaction(void) {
    return g_nav_compaction_required;
}

void navStateMarkDirty(void) {
    g_nav_compaction_required = true;
}

void navStateMarkCanonical(void) {
    g_nav_compaction_required = false;
}

void navStateValidateLoadedData(domino_fork_road_id_t* out_max_fork_id, domino_road_line_id_t* out_max_road_line_id) {
    const size_t fork_count = kv_size(domino_all_fork_road_list);
    const size_t road_line_count = kv_size(domino_all_road_line_list);
    if (road_line_count > DOMINO_NAV_ID_MAP_ENTITY_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "loaded road line count exceeds ID map capacity: count=%zu", road_line_count);
    }

    /* Storage 已建立 ID 到数组下标的映射并拒绝重复 ID，此处复用已加载的 map。 */
    domino_fork_road_id_t max_fork_id = 0U;
    for (size_t index = 0U; index < fork_count; index++) {
        const domino_fork_road_id_t fork_road_id = kv_A(domino_all_fork_road_list, index).id;
        if (fork_road_id == 0U || fork_road_id == UINT32_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "loaded fork road has invalid ID: index=%zu id=%" PRIu32, index, fork_road_id);
        }
        if (fork_road_id > max_fork_id) {
            max_fork_id = fork_road_id;
        }
    }

    domino_road_line_id_t max_road_line_id = 0U;
    for (size_t index = 0U; index < road_line_count; index++) {
        const domino_road_line_id_t road_line_id = kv_A(domino_all_road_line_list, index).id;
        if (road_line_id == 0U || road_line_id == UINT32_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "loaded road line has invalid ID: index=%zu id=%" PRIu32, index, road_line_id);
        }
        if (road_line_id > max_road_line_id) {
            max_road_line_id = road_line_id;
        }
    }

    *out_max_fork_id = max_fork_id;
    *out_max_road_line_id = max_road_line_id;
}

void navStateAdvanceNextIds(domino_fork_road_id_t max_fork_id, domino_road_line_id_t max_road_line_id) {
    advanceNextId(&g_domino_global_increment_id.fork_road_increment_id, max_fork_id);
    advanceNextId(&g_domino_global_increment_id.road_line_increment_id, max_road_line_id);
}

void navStateValidateRuntimeData(void) {
    const size_t fork_count = kv_size(domino_all_fork_road_list);
    const size_t road_count = kv_size(domino_all_road_list);
    if (kh_size(dominoForkRoadIdMap) != fork_count || kh_size(dominoRoadIdMap) != road_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "nav entity and ID map counts differ: forks=%zu fork_map=%zu roads=%zu road_map=%zu", fork_count,
                               (size_t)kh_size(dominoForkRoadIdMap), road_count, (size_t)kh_size(dominoRoadIdMap));
    }

    uint64_t adjacency_edge_count = 0U;
    for (size_t index = 0U; index < fork_count; index++) {
        const DominoForkRoad* fork_road = &kv_A(domino_all_fork_road_list, index);
        const khint_t map_slot = dominoForkRoadIdMap_get(dominoForkRoadIdMap, fork_road->id);
        if (fork_road->id == 0U || fork_road->id == UINT32_MAX || !kh_exist(dominoForkRoadIdMap, map_slot) ||
            kh_val(dominoForkRoadIdMap, map_slot) != index || fork_road->to_fork_road_count > MAX_EDGES_PER_VERTEX ||
            (fork_road->status != 0U && fork_road->to_fork_road_count != 0U)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "invalid fork road ID, mapping or adjacency: index=%zu id=%" PRIu32 " status=%u edges=%u", index,
                                   fork_road->id, (unsigned)fork_road->status, (unsigned)fork_road->to_fork_road_count);
        }

        adjacency_edge_count += fork_road->to_fork_road_count;
    }

    uint64_t passable_road_count = 0U;
    for (size_t index = 0U; index < road_count; index++) {
        const DominoRoad* road = &kv_A(domino_all_road_list, index);
        const domino_fork_road_id_t start_id = navExtractRoadStartForkRoadId(road->id);
        const domino_fork_road_id_t target_id = navExtractRoadTargetForkRoadId(road->id);
        const khint_t map_slot = dominoRoadIdMap_get(dominoRoadIdMap, road->id);
        if (start_id == target_id || !kh_exist(dominoRoadIdMap, map_slot) || kh_val(dominoRoadIdMap, map_slot) != index) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "invalid road endpoints or ID mapping: index=%zu id=%" PRIu64, index, road->id);
        }
        DominoForkRoad* start = nullptr;
        DominoForkRoad* target = nullptr;
        if (navStateFindForkRoadById(start_id, &start) == ERR_NOT_FOUND || navStateFindForkRoadById(target_id, &target) == ERR_NOT_FOUND) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "road references missing fork road: road=%" PRIu64 " start=%" PRIu32 " target=%" PRIu32,
                                   road->id, start_id, target_id);
        }
        if (road->status != 0U) {
            continue;
        }
        const bool is_passable = dominoNavEdgeWeightIsPassable(road->weight[0]);
        if (start->status != 0U || target->status != 0U || start->region_index != road->region_index || target->region_index != road->region_index ||
            start->type != road->type || target->type != road->type || navGraphHasEdge(start, target_id) != is_passable) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "road endpoints, network or adjacency are inconsistent: road=%" PRIu64 " region=%u network=%u",
                                   road->id, (unsigned)road->region_index, (unsigned)road->type);
        }
        passable_road_count += is_passable ? 1U : 0U;
    }
    /* ID map 的精确下标校验保证 road ID 唯一；每条可通行 road 都匹配一条不同的出边。
     * 总数相等即不存在额外或重复出边，无需再逐边查询端点和 road。 */
    if (adjacency_edge_count != passable_road_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "nav adjacency count differs from passable road count: edges=%" PRIu64 " roads=%" PRIu64,
                               adjacency_edge_count, passable_road_count);
    }
}

void navStateRefreshIdMapIndices(void) {
    const size_t fork_count = kv_size(domino_all_fork_road_list);
    const size_t road_count = kv_size(domino_all_road_list);

    /* 排序前已校验数组与 ID map 的一致性，排序只改变实体所在的数组下标。 */
    for (uint32_t index = 0U; index < (uint32_t)fork_count; index++) {
        DominoForkRoad* fork_road = &kv_A(domino_all_fork_road_list, index);
        const khint_t slot = dominoForkRoadIdMap_get(dominoForkRoadIdMap, fork_road->id);
        if (fork_road->id == 0U || !kh_exist(dominoForkRoadIdMap, slot)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "cannot refresh missing fork road ID mapping: id=%" PRIu32, fork_road->id);
        }
        kh_val(dominoForkRoadIdMap, slot) = index;
        fork_road->temp_index = 0U;
    }
    for (uint32_t index = 0U; index < (uint32_t)road_count; index++) {
        const DominoRoad* road = &kv_A(domino_all_road_list, index);
        const khint_t slot = dominoRoadIdMap_get(dominoRoadIdMap, road->id);
        if (road->id == 0U || !kh_exist(dominoRoadIdMap, slot)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "cannot refresh missing road ID mapping: id=%" PRIu64, road->id);
        }
        kh_val(dominoRoadIdMap, slot) = index;
    }
}

DOMINO_CODE navStateFindForkRoadById(domino_fork_road_id_t fork_road_id, DominoForkRoad** out_fork_road) {
    *out_fork_road = nullptr;
    if (fork_road_id == 0U) {
        return ERR_NOT_FOUND;
    }
    khint_t slot = dominoForkRoadIdMap_get(dominoForkRoadIdMap, fork_road_id);
    if (!kh_exist(dominoForkRoadIdMap, slot)) {
        return ERR_NOT_FOUND;
    }
    uint32_t index = kh_val(dominoForkRoadIdMap, slot);
    if (index >= kv_size(domino_all_fork_road_list) || kv_A(domino_all_fork_road_list, index).id != fork_road_id) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "fork road ID map points to an invalid entity: id=%" PRIu32 " index=%" PRIu32 " count=%zu",
                               fork_road_id, index, kv_size(domino_all_fork_road_list));
    }
    *out_fork_road = &kv_A(domino_all_fork_road_list, index);
    return CODE_OK;
}

DOMINO_CODE navStateFindRoadById(domino_road_id_t road_id, DominoRoad** out_road) {
    *out_road = nullptr;
    if (road_id == 0U) {
        return ERR_NOT_FOUND;
    }
    khint_t slot = dominoRoadIdMap_get(dominoRoadIdMap, road_id);
    if (!kh_exist(dominoRoadIdMap, slot)) {
        return ERR_NOT_FOUND;
    }
    uint32_t index = kh_val(dominoRoadIdMap, slot);
    if (index >= kv_size(domino_all_road_list) || kv_A(domino_all_road_list, index).id != road_id) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "road ID map points to an invalid entity: id=%" PRIu64 " index=%" PRIu32 " count=%zu", road_id,
                               index, kv_size(domino_all_road_list));
    }
    *out_road = &kv_A(domino_all_road_list, index);
    return CODE_OK;
}

DominoForkRoad* dominoGetForkRoadByID(domino_fork_road_id_t fork_road_id) {
    DominoForkRoad* fork_road = nullptr;
    return navStateFindForkRoadById(fork_road_id, &fork_road) == CODE_OK ? fork_road : nullptr;
}

DominoRoad* dominoGetRoadByID(domino_road_id_t road_id) {
    DominoRoad* road = nullptr;
    return navStateFindRoadById(road_id, &road) == CODE_OK ? road : nullptr;
}

DominoRoadLine* dominoGetRoadLineByID(domino_road_line_id_t road_line_id) {
    if (road_line_id == 0U) {
        return nullptr;
    }
    khint_t slot = dominoRoadLineIdMap_get(dominoRoadLineIdMap, road_line_id);
    if (!kh_exist(dominoRoadLineIdMap, slot)) {
        return nullptr;
    }
    uint32_t index = kh_val(dominoRoadLineIdMap, slot);
    if (index >= kv_size(domino_all_road_line_list) || kv_A(domino_all_road_line_list, index).id != road_line_id) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "road line ID map points to an invalid entity: id=%" PRIu32 " index=%" PRIu32 " count=%zu",
                               road_line_id, index, kv_size(domino_all_road_line_list));
    }
    return &kv_A(domino_all_road_line_list, index);
}

int navStateFindSegmentIndex(const DominoNavDataSegmentVec* segment_list, uint8_t region_index, uint8_t network_type) {
    for (size_t index = 0U; index < kv_size(*segment_list); index++) {
        const DominoNavDataSegment* segment = &kv_A(*segment_list, index);
        if (segment->region_index == region_index && segment->road_network_type == network_type) {
            return (int)index;
        }
    }
    return -1;
}

const DominoNavDataSegment* navStateFindSegment(const DominoNavDataSegmentVec* segment_list, uint8_t region_index, uint8_t network_type) {
    int segment_index = navStateFindSegmentIndex(segment_list, region_index, network_type);
    return segment_index < 0 ? nullptr : &kv_A(*segment_list, (size_t)segment_index);
}

void navStateInvalidateAllPathCaches(void) {
    for (size_t slot_index = 0U; slot_index < DOMINO_NAV_PATH_CACHE_SLOT_COUNT; slot_index++) {
        DominoNavPathCache* cache = g_nav_path_cache_slots[slot_index];
        if (cache) {
            navStateResetPathCache(cache);
        }
    }
}

void navStateInvalidatePathCache(uint8_t region_index, uint8_t road_network_type) {
    DominoNavPathCache* cache = g_nav_path_cache_slots[navStatePathCacheSlotIndex(region_index, road_network_type)];
    if (cache) {
        navStateResetPathCache(cache);
    }
}

const DominoNavPathCache* navStateGetPathCache(uint8_t region_index, uint8_t road_network_type) {
    const DominoNavPathCache* cache = g_nav_path_cache_slots[navStatePathCacheSlotIndex(region_index, road_network_type)];
    /* 缓存只通过 reset/commit 改写，非零 vertex_count 同时保证两个数组完整。 */
    return cache && cache->vertex_count != 0U ? cache : nullptr;
}

void navStateCommitPathCache(DominoNavPathCache* cache, PathInfo* path_matrix, DominoForkRoad** fork_road_list,
                             domino_nav_vertex_index_t vertex_count) {
    navStateResetPathCache(cache);
    cache->path_matrix = path_matrix;
    cache->fork_road_list = fork_road_list;
    cache->vertex_count = vertex_count;
}
