#ifndef DOMINO_ENGINE_NAV_MODEL_VIEW_H
#define DOMINO_ENGINE_NAV_MODEL_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_nav.h"
#include "klib/khashl.h"

/** @file model_view.h @brief Nav 内部类型、运行期索引与状态变量的统一声明。 */

#define DOMINO_NAV_ID_MAP_ENTITY_COUNT_MAX ((size_t)UINT32_C(1) << 30U)
#define DOMINO_FORK_ROAD_VEC_RESERVE_CAPACITY_MIN 1024u
#define DOMINO_ROAD_VEC_RESERVE_CAPACITY_MIN 1024u
#define DOMINO_ROAD_LINE_VEC_RESERVE_CAPACITY_MIN 1024u
#define DOMINO_NAV_PATH_CACHE_SLOT_COUNT ((size_t)UINT16_MAX + 1U)

#define WEIGHT_INFINITY UINT32_MAX  ///< 路径矩阵中的不可达权值（domino_nav_weight_t）
#define PRE_VERTEX_NULL UINT16_MAX  ///< 路径前驱顶点为空

/**
 * @brief 一个路网分组的持久化数组布局。
 *
 * 基础段由 `[init_index, init_index + init_count)` 表示；运行期新增实体位于
 * `[increment_index, entity_count)` 中，并按 `region_index` 与
 * `road_network_type` 过滤。该结构只描述存档协议，不包含任何运行期缓存。
 */
typedef struct {
    uint32_t init_index;
    uint32_t init_count;
    uint32_t increment_index;
    uint32_t total_count;
    uint8_t region_index;
    uint8_t road_network_type;
} DominoNavDataSegment;
static_assert(sizeof(DominoNavDataSegment) == 20U, "DominoNavDataSegment must contain only persisted layout fields");

typedef struct {
    domino_nav_vertex_index_t target_vertex_index;
    domino_nav_weight_atom_t weight;
} Edge;

typedef struct {
    Edge edges[MAX_EDGES_PER_VERTEX];
    domino_fork_road_id_t road_fork_id;
    uint8_t count;
} VertexEdges;

/** @brief Floyd 矩阵单元；当前只更新前驱 ID、前驱下标和 min_weight，其余字段预留。 */
typedef struct {
    domino_fork_road_id_t pre_road_fork_id;
    int32_t pre_road_fork_x;
    int32_t pre_road_fork_y;
    int32_t pre_road_fork_z;
    uint32_t total_hit_count;

    domino_nav_weight_t min_weight;

    uint16_t current_start_time;
    domino_nav_vertex_index_t pre_vertex_index;
    uint16_t current_hit_count;

    uint8_t middle_vertex_count;
    uint8_t state_info;
} PathInfo;

static_assert(sizeof(PathInfo) == 32, "PathInfo must be exactly 32 bytes");

typedef struct {
    double init_time_used;
    double core_time_used;
    long total_tick;
    long hit_tick;
    long null_weight_count;
    long null_weight_count_inner;
} FloydPathPlanningResult;

/** @brief 拥有路径矩阵和实体指针表的路网缓存。 */
typedef struct {
    PathInfo* path_matrix;
    DominoForkRoad** fork_road_list;
    domino_nav_vertex_index_t vertex_count;
} DominoNavPathCache;

/** @brief 从实体中读取状态、地域或路网类型字段。 */
typedef uint8_t (*DominoNavGetU8Fn)(const void* element);

KHASHL_MAP_INIT(KH_LOCAL, DominoForkRoadIDMap, dominoForkRoadIdMap, domino_fork_road_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoRoadIDMap, dominoRoadIdMap, domino_road_id_t, uint32_t, kh_hash_uint64, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoRoadLineIDMap, dominoRoadLineIdMap, domino_road_line_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

/** @brief Nav 实体 ID 到数组下标的运行期映射。 */
extern DominoForkRoadIDMap* dominoForkRoadIdMap;
extern DominoRoadIDMap* dominoRoadIdMap;
extern DominoRoadLineIDMap* dominoRoadLineIdMap;

#endif
