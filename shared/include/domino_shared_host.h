#ifndef DOMINO_SHARED_HOST_H
#define DOMINO_SHARED_HOST_H
#include <stdatomic.h>
#include <stdint.h>

#include "domino_shared_types.h"

/**
 * @file domino_shared_host.h
 * @brief 宿主世界的地域、岛屿、运动物体和全局自增 ID 类型。
 */

/* 规划的日历容量：1440 时间槽 × 12 月 × 242 年；当前 time 模块尚未执行日历换算。 */
#define DOMINO_HOST_WORLD_RUN_MAX_TIME 4181760

#define DOMINO_MAX_REGION_COUNT 8
#define DOMINO_MAX_ISLAND_COUNT_PER_REGION 64
#define DOMINO_MAX_COUNTRY_COUNT_PER_REGION 64
#define DOMINO_MAX_CITY_COUNT_PER_REGION 128

#define DOMINO_MAX_HUMAN_COUNT_PER_REGION 10240
#define DOMINO_MAX_ORG_COUNT_PER_REGION DOMINO_MAX_HUMAN_COUNT_PER_REGION

// 用于热点空间划分16*16=256个空间，用于物理运动和分布统计
typedef uint8_t domino_space_id_t;
// 用于热点区域划分16*16=256个区域，用于物理运动和分布统计
typedef uint8_t domino_space_area_id_t;

typedef struct {
    /* 计数器随根 meta 持久化，0 不作为实体 ID；独立对齐以减少不同计数器间的伪共享。 */
    alignas(128) _Atomic domino_human_id_t human_increment_id;
    alignas(128) _Atomic domino_org_id_t org_increment_id;
    alignas(128) _Atomic domino_country_id_t country_increment_id;
    alignas(128) _Atomic domino_city_id_t city_increment_id;
    alignas(128) _Atomic domino_island_id_t island_increment_id;
    alignas(128) _Atomic domino_building_id_t building_increment_id;
    alignas(128) _Atomic domino_fork_road_id_t fork_road_increment_id;
    alignas(128) _Atomic domino_asset_id_t asset_increment_id;
    alignas(128) _Atomic domino_movable_object_id_t movable_object_increment_id;
    alignas(128) _Atomic domino_name_id_t name_increment_id;
    alignas(128) _Atomic domino_description_id_t description_increment_id;
    alignas(128) _Atomic domino_road_line_id_t road_line_increment_id;
} DominoGlobalIncrementID;

/**
 * @brief 从全局原子递增计数器中分配一个非 0 ID（0 保留为无效值）
 * @param counter_atomic DominoGlobalIncrementID 中的原子计数器字段
 * @param out_id_lvalue 输出 ID 左值变量
 */
#define DOMINO_ALLOC_NON_ZERO_ID(counter_atomic, out_id_lvalue)                                       \
    do {                                                                                              \
        (out_id_lvalue) = atomic_fetch_add_explicit(&(counter_atomic), 1U, memory_order_relaxed);     \
        if ((out_id_lvalue) == 0U) {                                                                  \
            (out_id_lvalue) = atomic_fetch_add_explicit(&(counter_atomic), 1U, memory_order_relaxed); \
        }                                                                                             \
    } while (0)

typedef struct {
    /* regions.json 只保存 g_domino_region_list 的有效前缀。 */
    alignas(128) _Atomic int32_t human_count;
    alignas(128) _Atomic int32_t org_count;

    alignas(128) _Atomic int32_t country_count;
    alignas(128) _Atomic int32_t city_count;

    alignas(128) _Atomic int32_t island_count;
    alignas(128) _Atomic int32_t building_count;
    alignas(128) _Atomic int32_t asset_count;
    alignas(128) _Atomic int32_t fork_road_count;

    domino_type_t type;
    domino_status_t status;

} DominoRegion;

typedef struct {
    domino_island_id_t id;

    domino_type_t type;
    domino_status_t status;

    uint8_t region_index;

    uint8_t other_info[121];  ///< 预留，不持久化。
} DominoIsland;

static_assert(sizeof(DominoIsland) == 128, "DominoIsland must be exactly 128 bytes");

typedef struct {
    domino_movable_object_id_t id;
    int32_t position_x;
    int32_t position_y;
    int32_t position_z;  // 高度
    float direction[3];

    uint8_t speed;
    int8_t acceleration;

    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;
    uint8_t other_info[32];  ///< 预留，不持久化。
} DominoMovableObject;

static_assert(sizeof(DominoMovableObject) == 64, "MovableObject must be exactly 64 bytes");

#endif
