#include "entry.h"

#include <stddef.h>

#include "domino_shared_common.h"

DominoIdCounters g_domino_id_counters;
DominoRegion g_domino_region_list[DOMINO_MAX_REGION_COUNT];

DominoIslandVec g_domino_island_list;
DominoMovableObjectVec g_domino_movable_object_list;

DominoIslandIdMap* g_domino_island_id_map;
DominoMovableObjectIdMap* g_domino_movable_object_id_map;

uint32_t g_domino_region_count = 0;

/** @brief 清零全局 ID 计数器和地域状态；原子成员须逐项写入。 */
static void resetWorldState(void) {
    g_domino_region_count = 0;

    atomic_store_explicit(&g_domino_id_counters.next_human_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.last_motivation_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.last_task_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.last_behavior_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_org_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_country_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_city_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_island_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_building_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_fork_road_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_asset_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_movable_object_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_name_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_description_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_id_counters.next_road_line_id, 0U, memory_order_relaxed);

    for (uint32_t region_index = 0; region_index < (uint32_t)DOMINO_MAX_REGION_COUNT; region_index++) {
        DominoRegion* region_ptr = &g_domino_region_list[region_index];
        atomic_store_explicit(&region_ptr->human_count, 0U, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->org_count, 0U, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->country_count, 0U, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->city_count, 0U, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->island_count, 0U, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->building_count, 0U, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->asset_count, 0U, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->fork_road_count, 0U, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->movable_object_count, 0U, memory_order_relaxed);
        region_ptr->type = 0;
        region_ptr->status = 0;
    }
}

DominoIsland* dominoGetIslandById(domino_island_id_t island_id) {
    khint_t key = dominoIslandIdMap_get(g_domino_island_id_map, island_id);
    if (kh_exist(g_domino_island_id_map, key)) {
        return &kv_A(g_domino_island_list, kh_val(g_domino_island_id_map, key));
    }
    return nullptr;
}

DominoMovableObject* dominoGetMovableObjectById(domino_movable_object_id_t movable_object_id) {
    khint_t key = dominoMovableObjectIdMap_get(g_domino_movable_object_id_map, movable_object_id);
    if (kh_exist(g_domino_movable_object_id_map, key)) {
        return &kv_A(g_domino_movable_object_list, kh_val(g_domino_movable_object_id_map, key));
    }
    return nullptr;
}

void dominoHostModuleInit(void) {
    g_domino_island_id_map = dominoIslandIdMap_init();
    if (!g_domino_island_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate island ID map");
    }
    g_domino_movable_object_id_map = dominoMovableObjectIdMap_init();
    if (!g_domino_movable_object_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate movable object ID map");
    }
}

void dominoHostModuleDestroy(void) {
    dominoMovableObjectIdMap_destroy(g_domino_movable_object_id_map);
    g_domino_movable_object_id_map = nullptr;
    dominoIslandIdMap_destroy(g_domino_island_id_map);
    g_domino_island_id_map = nullptr;
    kv_destroy(g_domino_movable_object_list);
    kv_init(g_domino_movable_object_list);
    kv_destroy(g_domino_island_list);
    kv_init(g_domino_island_list);
    resetWorldState();
}
