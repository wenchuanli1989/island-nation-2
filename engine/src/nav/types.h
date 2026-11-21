#ifndef DOMINO_ENGINE_NAV_TYPES_H
#define DOMINO_ENGINE_NAV_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_nav.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @file types.h
 * @brief Nav 内部类型：存档分段、Floyd 邻接与矩阵、路径缓存、道路 ID 编码、ID 映射表和实体容器类型。
 */

#define DOMINO_NAV_ID_MAP_ENTITY_COUNT_MAX ((size_t)UINT32_C(1) << 30U)
#define DOMINO_NAV_PATH_CACHE_SLOT_COUNT ((size_t)UINT16_MAX + 1U)

#define DOMINO_NAV_WEIGHT_INFINITY UINT32_MAX  ///< 路径矩阵中的不可达权值。
#define DOMINO_NAV_VERTEX_NONE UINT16_MAX      ///< 没有前驱顶点。

/** @brief 一个路网分组（region_index, road_network_type）的存档布局，只含持久化字段。
 * @note 基础段为 [base_index, base_index + base_count)；运行期新增的实体位于 [tail_index, 数组末尾)，按分组键过滤。
 */
typedef struct {
    uint32_t base_index;
    uint32_t base_count;
    uint32_t tail_index;
    uint32_t total_count;  ///< 分组内的活跃实体数。
    uint8_t region_index;
    uint8_t road_network_type;
} DominoNavSegment;
static_assert(sizeof(DominoNavSegment) == 20U, "DominoNavSegment must contain only persisted layout fields");

typedef struct {
    domino_nav_vertex_index_t target_vertex_index;
    domino_nav_edge_weight_t weight;
} DominoNavEdge;

/** @brief Floyd 输入：一个顶点的出边。 */
typedef struct {
    DominoNavEdge edge_list[DOMINO_MAX_EDGE_COUNT_PER_VERTEX];
    domino_fork_road_id_t fork_road_id;
    uint8_t edge_count;
} DominoNavVertexEdges;

/** @brief Floyd 路径矩阵单元：起点到本顶点的最短权重和最后一跳的前驱；矩阵为 vertex_count² 个单元，保持紧凑。 */
typedef struct {
    domino_nav_weight_t min_weight;
    domino_fork_road_id_t pre_fork_road_id;
    domino_nav_vertex_index_t pre_vertex_index;
} DominoNavPathCell;
static_assert(sizeof(DominoNavPathCell) == 12, "DominoNavPathCell must stay 12 bytes");

/** @brief 一次 Floyd 构建的耗时（clock() 秒）和松弛计数。 */
typedef struct {
    double init_time_s;
    double core_time_s;
    long relax_check_count;
    long relax_hit_count;
    long unreachable_source_count;  ///< 起点到中间点不可达而跳过的行数。
    long unreachable_target_count;  ///< 中间点到终点不可达而跳过的单元数。
} DominoNavFloydStats;

/** @brief 一个路网分组的路径缓存，拥有路径矩阵和顶点到实体的指针表。 */
typedef struct {
    DominoNavPathCell* path_matrix;
    DominoForkRoad** fork_road_list;
    domino_nav_vertex_index_t vertex_count;  ///< 0 表示缓存无效。
} DominoNavPathCache;

/* 道路 ID 编码：高 32 位为起点路口 ID，低 32 位为终点路口 ID。 */
static inline domino_road_id_t navBuildRoadId(domino_fork_road_id_t start_fork_road_id, domino_fork_road_id_t target_fork_road_id) {
    return ((domino_road_id_t)start_fork_road_id << 32U) | (domino_road_id_t)target_fork_road_id;
}

static inline domino_fork_road_id_t navExtractRoadStartForkRoadId(domino_road_id_t road_id) {
    return (domino_fork_road_id_t)(road_id >> 32U);
}

static inline domino_fork_road_id_t navExtractRoadTargetForkRoadId(domino_road_id_t road_id) {
    return (domino_fork_road_id_t)(road_id & UINT32_MAX);
}

/** @brief 从实体读取状态、地域或路网类型字段。 */
typedef uint8_t (*DominoNavGetU8Fn)(const void* element);

KHASHL_MAP_INIT(KH_LOCAL, DominoForkRoadIdMap, dominoForkRoadIdMap, domino_fork_road_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoRoadIdMap, dominoRoadIdMap, domino_road_id_t, uint32_t, kh_hash_uint64, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoRoadLineIdMap, dominoRoadLineIdMap, domino_road_line_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(DominoNavSegment) DominoNavSegmentVec;
typedef kvec_t(DominoForkRoad) DominoForkRoadVec;
typedef kvec_t(DominoRoad) DominoRoadVec;
typedef kvec_t(DominoRoadLine) DominoRoadLineVec;

#endif
