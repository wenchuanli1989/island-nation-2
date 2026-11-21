#ifndef DOMINO_SHARED_HOST_H
#define DOMINO_SHARED_HOST_H
#include <stdatomic.h>
#include <stdint.h>

#include "domino_shared_types.h"

/**
 * @file domino_shared_host.h
 * @brief 宿主世界的地域、岛屿、运动物体和全局自增 ID 类型。
 */

#define DOMINO_MAX_REGION_COUNT 8
#define DOMINO_MAX_ISLAND_COUNT_PER_REGION 64
#define DOMINO_MAX_COUNTRY_COUNT_PER_REGION 64
#define DOMINO_MAX_CITY_COUNT_PER_REGION 128

#define DOMINO_MAX_HUMAN_COUNT_PER_REGION 10240
#define DOMINO_MAX_ORG_COUNT_PER_REGION DOMINO_MAX_HUMAN_COUNT_PER_REGION

typedef uint8_t domino_space_id_t;       ///< 16×16 热点空间划分，用于物理运动和分布统计。
typedef uint8_t domino_space_area_id_t;  ///< 16×16 热点区域划分，用于物理运动和分布统计。

/** @brief 全局 ID 计数器，随根 meta 的 id_counters 持久化（key 与字段同名）；各计数器独立缓存行对齐以减少伪共享。
 * @note last_* 为最后已分配 ID（动机、任务、行为），只由 human 执行器或串行阶段递增，耗尽致命退出；
 * next_* 为下一个待分配 ID，经 DOMINO_ALLOC_NON_ZERO_ID 分配，允许回绕并跳过 0。
 */
typedef struct {
    alignas(128) _Atomic domino_human_id_t next_human_id;
    alignas(128) _Atomic domino_motivation_id_t last_motivation_id;
    alignas(128) _Atomic domino_task_id_t last_task_id;
    alignas(128) _Atomic domino_behavior_id_t last_behavior_id;
    alignas(128) _Atomic domino_org_id_t next_org_id;
    alignas(128) _Atomic domino_country_id_t next_country_id;
    alignas(128) _Atomic domino_city_id_t next_city_id;
    alignas(128) _Atomic domino_island_id_t next_island_id;
    alignas(128) _Atomic domino_building_id_t next_building_id;
    alignas(128) _Atomic domino_fork_road_id_t next_fork_road_id;
    alignas(128) _Atomic domino_asset_id_t next_asset_id;
    alignas(128) _Atomic domino_movable_object_id_t next_movable_object_id;
    alignas(128) _Atomic domino_name_id_t next_name_id;
    alignas(128) _Atomic domino_description_id_t next_description_id;
    alignas(128) _Atomic domino_road_line_id_t next_road_line_id;
} DominoIdCounters;

/** @brief 从可回绕的原子计数器取一个非 0 ID 写入 out_id_lvalue（0 保留为无效值）。 */
#define DOMINO_ALLOC_NON_ZERO_ID(counter_atomic, out_id_lvalue)                                       \
    do {                                                                                              \
        (out_id_lvalue) = atomic_fetch_add_explicit(&(counter_atomic), 1U, memory_order_relaxed);     \
        if ((out_id_lvalue) == 0U) {                                                                  \
            (out_id_lvalue) = atomic_fetch_add_explicit(&(counter_atomic), 1U, memory_order_relaxed); \
        }                                                                                             \
    } while (0)

/** @brief 地域统计；regions.json 只保存 g_domino_region_list 的有效前缀。 */
typedef struct {
    alignas(128) _Atomic uint32_t human_count;
    alignas(128) _Atomic uint32_t org_count;

    alignas(128) _Atomic uint32_t country_count;
    alignas(128) _Atomic uint32_t city_count;

    alignas(128) _Atomic uint32_t island_count;
    alignas(128) _Atomic uint32_t building_count;
    alignas(128) _Atomic uint32_t asset_count;
    alignas(128) _Atomic uint32_t fork_road_count;

    alignas(128) _Atomic uint32_t movable_object_count;

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
    int32_t position_z;  ///< 高度。
    float direction[3];

    uint8_t speed;
    int8_t acceleration;

    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;
    uint8_t other_info[32];  ///< 预留，不持久化。
} DominoMovableObject;

static_assert(sizeof(DominoMovableObject) == 64, "MovableObject must be exactly 64 bytes");

#endif
