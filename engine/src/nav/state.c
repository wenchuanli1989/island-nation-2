#include "state.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../host/entry.h"
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

static bool g_nav_state_initialized = false;
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
        if (cache) {
            g_nav_path_cache_slots[slot_index] = cache;
        }
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

DOMINO_CODE navStateInit(void) {
    if (g_nav_state_initialized) {
        return ERR_ALREADY_INITIALIZED;
    }

    kv_init(domino_all_fork_road_list);
    kv_init(domino_all_road_list);
    kv_init(domino_all_road_line_list);
    kv_init(domino_all_fork_road_nav_data_segment_list);
    kv_init(domino_all_road_nav_data_segment_list);
    navStateDestroyPathCaches();

    dominoForkRoadIdMap = dominoForkRoadIdMap_init();
    dominoRoadIdMap = dominoRoadIdMap_init();
    dominoRoadLineIdMap = dominoRoadLineIdMap_init();
    if (!dominoForkRoadIdMap || !dominoRoadIdMap || !dominoRoadLineIdMap) {
        navStateDestroy();
        return ERR_MEMORY_ALLOC;
    }

    kv_resize(DominoForkRoad, domino_all_fork_road_list, DOMINO_FORK_ROAD_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoRoad, domino_all_road_list, DOMINO_ROAD_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoRoadLine, domino_all_road_line_list, DOMINO_ROAD_LINE_VEC_RESERVE_CAPACITY_MIN);

    g_nav_state_initialized = true;
    g_nav_compaction_required = true;
    return CODE_OK;
}

void navStateDestroy(void) {
    navStateDestroyPathCaches();

    for (size_t index = 0U; index < kv_size(domino_all_road_list); index++) {
        DominoRoad* road = &kv_A(domino_all_road_list, index);
        if (road->corner_point_count > MIN_CORNER_POINTS_PER_ROAD) {
            free(road->corner_point_ptr);
        }
    }

    if (dominoForkRoadIdMap) {
        dominoForkRoadIdMap_destroy(dominoForkRoadIdMap);
    }
    if (dominoRoadIdMap) {
        dominoRoadIdMap_destroy(dominoRoadIdMap);
    }
    if (dominoRoadLineIdMap) {
        dominoRoadLineIdMap_destroy(dominoRoadLineIdMap);
    }
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

    g_nav_state_initialized = false;
    g_nav_compaction_required = true;
}

bool navStateIsReady(void) {
    /* 仅在三个 ID map 和实体容器完成初始化后置 true。Nav 生命周期操作须串行执行。 */
    return g_nav_state_initialized;
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

DOMINO_CODE navStateValidateLoadedData(domino_fork_road_id_t* out_max_fork_id, domino_road_line_id_t* out_max_road_line_id) {
    const size_t fork_count = kv_size(domino_all_fork_road_list);
    const size_t road_line_count = kv_size(domino_all_road_line_list);
    if (road_line_count > DOMINO_NAV_ID_MAP_ENTITY_COUNT_MAX) {
        return ERR_OUT_OF_RANGE;
    }

    /* Storage 已建立 ID 到数组下标的映射并拒绝重复 ID，此处复用已加载的 map。 */
    domino_fork_road_id_t max_fork_id = 0U;
    for (size_t index = 0U; index < fork_count; index++) {
        const domino_fork_road_id_t fork_road_id = kv_A(domino_all_fork_road_list, index).id;
        if (fork_road_id == 0U || fork_road_id == UINT32_MAX) {
            return fork_road_id == UINT32_MAX ? ERR_OUT_OF_RANGE : ERR_INVALID_DATA;
        }
        if (fork_road_id > max_fork_id) {
            max_fork_id = fork_road_id;
        }
    }

    domino_road_line_id_t max_road_line_id = 0U;
    for (size_t index = 0U; index < road_line_count; index++) {
        const domino_road_line_id_t road_line_id = kv_A(domino_all_road_line_list, index).id;
        if (road_line_id == 0U || road_line_id == UINT32_MAX) {
            return road_line_id == UINT32_MAX ? ERR_OUT_OF_RANGE : ERR_INVALID_DATA;
        }
        if (road_line_id > max_road_line_id) {
            max_road_line_id = road_line_id;
        }
    }

    *out_max_fork_id = max_fork_id;
    *out_max_road_line_id = max_road_line_id;
    return CODE_OK;
}

void navStateAdvanceNextIds(domino_fork_road_id_t max_fork_id, domino_road_line_id_t max_road_line_id) {
    advanceNextId(&g_domino_global_increment_id.fork_road_increment_id, max_fork_id);
    advanceNextId(&g_domino_global_increment_id.road_line_increment_id, max_road_line_id);
}

DOMINO_CODE navStateValidateRuntimeData(void) {
    const size_t fork_count = kv_size(domino_all_fork_road_list);
    const size_t road_count = kv_size(domino_all_road_list);
    if (kh_size(dominoForkRoadIdMap) != fork_count || kh_size(dominoRoadIdMap) != road_count) {
        return ERR_INVALID_DATA;
    }

    uint64_t adjacency_edge_count = 0U;
    for (size_t index = 0U; index < fork_count; index++) {
        const DominoForkRoad* fork_road = &kv_A(domino_all_fork_road_list, index);
        const khint_t map_slot = dominoForkRoadIdMap_get(dominoForkRoadIdMap, fork_road->id);
        if (fork_road->id == 0U || fork_road->id == UINT32_MAX || !kh_exist(dominoForkRoadIdMap, map_slot) ||
            kh_val(dominoForkRoadIdMap, map_slot) != index || fork_road->to_fork_road_count > MAX_EDGES_PER_VERTEX ||
            (fork_road->status != 0U && fork_road->to_fork_road_count != 0U)) {
            return ERR_INVALID_DATA;
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
            return ERR_INVALID_DATA;
        }
        const DominoForkRoad* start = dominoGetForkRoadByID(start_id);
        const DominoForkRoad* target = dominoGetForkRoadByID(target_id);
        if (!start || !target) {
            return ERR_INVALID_DATA;
        }
        if (road->status != 0U) {
            continue;
        }
        const bool is_passable = dominoNavEdgeWeightIsPassable(road->weight[0]);
        if (start->status != 0U || target->status != 0U || start->region_index != road->region_index || target->region_index != road->region_index ||
            start->type != road->type || target->type != road->type || navGraphHasEdge(start, target_id) != is_passable) {
            return ERR_INVALID_DATA;
        }
        passable_road_count += is_passable ? 1U : 0U;
    }
    /* ID map 的精确下标校验保证 road ID 唯一；每条可通行 road 都匹配一条不同的出边。
     * 总数相等即不存在额外或重复出边，无需再逐边查询端点和 road。 */
    return adjacency_edge_count == passable_road_count ? CODE_OK : ERR_INVALID_DATA;
}

void navStateRefreshIdMapIndices(void) {
    const size_t fork_count = kv_size(domino_all_fork_road_list);
    const size_t road_count = kv_size(domino_all_road_list);

    /* 排序前已校验数组与 ID map 的一致性，排序只改变实体所在的数组下标。 */
    for (uint32_t index = 0U; index < (uint32_t)fork_count; index++) {
        DominoForkRoad* fork_road = &kv_A(domino_all_fork_road_list, index);
        const khint_t slot = dominoForkRoadIdMap_get(dominoForkRoadIdMap, fork_road->id);
        assert(fork_road->id != 0U && kh_exist(dominoForkRoadIdMap, slot));
        kh_val(dominoForkRoadIdMap, slot) = index;
        fork_road->temp_index = 0U;
    }
    for (uint32_t index = 0U; index < (uint32_t)road_count; index++) {
        const DominoRoad* road = &kv_A(domino_all_road_list, index);
        const khint_t slot = dominoRoadIdMap_get(dominoRoadIdMap, road->id);
        assert(road->id != 0U && kh_exist(dominoRoadIdMap, slot));
        kh_val(dominoRoadIdMap, slot) = index;
    }
}

DominoForkRoad* dominoGetForkRoadByID(domino_fork_road_id_t fork_road_id) {
    if (!navStateIsReady() || fork_road_id == 0U) {
        return nullptr;
    }
    khint_t slot = dominoForkRoadIdMap_get(dominoForkRoadIdMap, fork_road_id);
    if (!kh_exist(dominoForkRoadIdMap, slot)) {
        return nullptr;
    }
    uint32_t index = kh_val(dominoForkRoadIdMap, slot);
    if (index >= kv_size(domino_all_fork_road_list) || kv_A(domino_all_fork_road_list, index).id != fork_road_id) {
        return nullptr;
    }
    return &kv_A(domino_all_fork_road_list, index);
}

DominoRoad* dominoGetRoadByID(domino_road_id_t road_id) {
    if (!navStateIsReady() || road_id == 0U) {
        return nullptr;
    }
    khint_t slot = dominoRoadIdMap_get(dominoRoadIdMap, road_id);
    if (!kh_exist(dominoRoadIdMap, slot)) {
        return nullptr;
    }
    uint32_t index = kh_val(dominoRoadIdMap, slot);
    if (index >= kv_size(domino_all_road_list) || kv_A(domino_all_road_list, index).id != road_id) {
        return nullptr;
    }
    return &kv_A(domino_all_road_list, index);
}

DominoRoadLine* dominoGetRoadLineByID(domino_road_line_id_t road_line_id) {
    if (!navStateIsReady() || road_line_id == 0U) {
        return nullptr;
    }
    khint_t slot = dominoRoadLineIdMap_get(dominoRoadLineIdMap, road_line_id);
    if (!kh_exist(dominoRoadLineIdMap, slot)) {
        return nullptr;
    }
    uint32_t index = kh_val(dominoRoadLineIdMap, slot);
    if (index >= kv_size(domino_all_road_line_list) || kv_A(domino_all_road_line_list, index).id != road_line_id) {
        return nullptr;
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
