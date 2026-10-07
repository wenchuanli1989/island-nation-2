#include "entry.h"

#include <stddef.h>

#include "../logger/entry.h"

DominoGlobalIncrementID g_domino_global_increment_id;
DominoRegion g_domino_region_list[DOMINO_MAX_REGION_COUNT];

DominoIslandVec domino_all_island_list;
DominoMovableObjectVec domino_all_movable_object_list;

DominoIslandIDMap* dominoIslandIdMap;
DominoMovableObjectIDMap* dominoMovableObjectIdMap;

uint32_t g_domino_region_count = 0;

/** @brief 在线程启动前或停止后清空世界固定状态，原子成员逐项复位。 */
static void dominoHostResetWorldState(void) {
    g_domino_region_count = 0;

    atomic_store_explicit(&g_domino_global_increment_id.human_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.org_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.country_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.city_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.island_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.building_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.fork_road_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.asset_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.movable_object_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.name_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.description_increment_id, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_global_increment_id.road_line_increment_id, 0U, memory_order_relaxed);

    for (uint32_t region_index = 0; region_index < (uint32_t)DOMINO_MAX_REGION_COUNT; region_index++) {
        DominoRegion* region_ptr = &g_domino_region_list[region_index];
        atomic_store_explicit(&region_ptr->human_count, 0, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->org_count, 0, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->country_count, 0, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->city_count, 0, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->island_count, 0, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->building_count, 0, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->asset_count, 0, memory_order_relaxed);
        atomic_store_explicit(&region_ptr->fork_road_count, 0, memory_order_relaxed);
        region_ptr->type = 0;
        region_ptr->status = 0;
    }
}

DominoIsland* dominoGetIslandByID(domino_island_id_t island_id) {
    khint_t key = dominoIslandIdMap_get(dominoIslandIdMap, island_id);
    if (kh_exist(dominoIslandIdMap, key)) {
        return &kv_A(domino_all_island_list, kh_val(dominoIslandIdMap, key));
    }
    return nullptr;
}

DominoMovableObject* dominoGetMovableObjectByID(domino_movable_object_id_t movable_object_id) {
    khint_t key = dominoMovableObjectIdMap_get(dominoMovableObjectIdMap, movable_object_id);
    if (kh_exist(dominoMovableObjectIdMap, key)) {
        return &kv_A(domino_all_movable_object_list, kh_val(dominoMovableObjectIdMap, key));
    }
    return nullptr;
}

void dominoHostModuleInit(void) {
    dominoHostResetWorldState();

    kv_init(domino_all_island_list);
    kv_init(domino_all_movable_object_list);
    kv_resize(DominoIsland, domino_all_island_list, HOST_ISLAND_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoMovableObject, domino_all_movable_object_list, HOST_MOVABLE_OBJECT_VEC_RESERVE_CAPACITY_MIN);
    dominoIslandIdMap = dominoIslandIdMap_init();
    dominoMovableObjectIdMap = dominoMovableObjectIdMap_init();

    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_HOST, DOMINO_LOG_LEVEL_INFO, "hostModuleInit");
}

void dominoHostModuleExit(void) {
    dominoMovableObjectIdMap_destroy(dominoMovableObjectIdMap);
    dominoIslandIdMap_destroy(dominoIslandIdMap);
    kv_destroy(domino_all_movable_object_list);
    kv_destroy(domino_all_island_list);
    dominoHostResetWorldState();
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_HOST, DOMINO_LOG_LEVEL_INFO, "hostModuleExit");
}
