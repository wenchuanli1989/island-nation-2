#ifndef DOMINO_SHARED_NAV_H
#define DOMINO_SHARED_NAV_H

#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

/**
 * @file domino_shared_nav.h
 * @brief 导航实体和道路图公共类型。
 *
 * engine nav 模块按 `(region_index, road_network_type)` 对 fork road 与 road 分段，并为每个分组按需维护 Floyd 路径缓存。
 */

#define MAX_EDGES_PER_VERTEX 9
#define MIN_CORNER_POINTS_PER_ROAD 8
#define MAX_FORK_ROAD_COUNT_PER_ROAD_LINE 14
#define MAX_FORK_ROAD_COUNT_PER_NETWORK 3000  ///< 每个 (region_index, network_type) 分组的活跃路口上限。

#define DOMINO_NAV_EDGE_WEIGHT_IMPASSABLE UINT8_MAX  ///< 不可通行边权值

typedef uint32_t domino_nav_weight_t;
typedef uint8_t domino_nav_weight_atom_t;
typedef uint16_t domino_nav_vertex_index_t;
static_assert(MAX_FORK_ROAD_COUNT_PER_NETWORK <= UINT16_MAX, "network vertex limit must fit domino_nav_vertex_index_t");

/** @brief 路网类型位标记；当前实体的 `type` 字段直接保存该值或调用方约定的默认类型。 */
typedef enum {
    ROAD_NETWORK_TYPE_ROAD = 0b00000001,
    ROAD_NETWORK_TYPE_RAILWAY = 0b00000010,
    ROAD_NETWORK_TYPE_WATERWAY = 0b00000100,
    ROAD_NETWORK_TYPE_AIRWAY = 0b00001000,
    ROAD_NETWORK_TYPE_HIGHWAY = 0b00010000,
} DOMINO_ROAD_NETWORK;
typedef struct DominoForkRoad {
    domino_fork_road_id_t id;

    int32_t position_x;
    int32_t position_y;
    int32_t position_z;

    domino_fork_road_id_t to_fork_road_id[MAX_EDGES_PER_VERTEX];

    /* 路径缓存中的顶点下标；与邻接表一起在加载后重建，不持久化。 */
    domino_nav_vertex_index_t temp_index;

    domino_nav_weight_atom_t weight;  ///< 节点权重字段；当前 Floyd 仅使用 road.weight[0]。
    uint8_t region_index;
    uint8_t to_fork_road_count;
    domino_status_t status;  ///< fork road / road 中 0 为活跃，非 0 为逻辑删除。

    domino_type_t type;     ///< 路网分组类型，与 road_network_type 一致。
    uint8_t num[2];         ///< 固定两个字节，不是 C 字符串。
    uint8_t other_info[3];  ///< 预留，不持久化。
} DominoForkRoad;
static_assert(sizeof(DominoForkRoad) == 64, "DominoForkRoad must be exactly 64 bytes");

typedef struct DominoRoad {
    /* 拐点数超过内联容量时使用堆数组 [点][轴]，否则使用内联数组 [轴][点]。 */
    union {
        int32_t (*corner_point_ptr)[3];
        int32_t corner_points[3][MIN_CORNER_POINTS_PER_ROAD];
    };
    domino_road_id_t id;  ///< 高 32 位为起点 ID，低 32 位为终点 ID。

    uint32_t distance;
    uint16_t corner_point_count;

    uint8_t time[4];
    uint8_t cost;
    uint8_t speed[4];

    domino_nav_weight_atom_t weight[4];  ///< 当前寻路仅使用 weight[0]；0 和 UINT8_MAX 表示不可通行。

    domino_type_t type;      ///< 路网分组类型，与 road_network_type 一致。
    domino_status_t status;  ///< fork road / road 中 0 为活跃，非 0 为逻辑删除。
    uint8_t region_index;
    uint8_t num[2];  ///< 固定两个字节，不是 C 字符串。
} DominoRoad;
static_assert(sizeof(DominoRoad) == 128, "DominoRoad must be exactly 128 bytes");

/**
 * @brief 道路线，由多个 fork road 中间节点组成。
 *
 * `dominoNavigation()` 会根据 start/current/target 信息填充路径、起终点 fork road 和剩余距离等字段。
 */
typedef struct DominoRoadLine {
    domino_road_line_id_t id;
    domino_fork_road_id_t fork_road_id_list[MAX_FORK_ROAD_COUNT_PER_ROAD_LINE];  ///< 按行进顺序排列的中间节点，不含起终点。

    DominoLocationXY start_location;
    DominoLocationXY target_location;
    DominoLocationXY current_location;
    domino_fork_road_id_t start_fork_road_id;
    domino_fork_road_id_t target_fork_road_id;
    domino_fork_road_id_t current_fork_road_id;

    uint32_t used_time;
    uint32_t remaining_time;
    uint32_t passed_distance;
    uint32_t remaining_distance;  ///< 当前导航写入最短路径权重，尚未换算为物理距离。
    uint32_t consumed_cost;
    uint32_t remaining_cost;
    uint16_t fork_road_count;

    uint8_t road_index;
    domino_type_t type;  ///< 路网分组类型。
    domino_status_t status;

    uint8_t region_index;
    uint8_t other_info[2];  ///< 预留，不持久化。

} DominoRoadLine;
static_assert(sizeof(DominoRoadLine) == 128, "DominoRoadLine must be exactly 128 bytes");

static inline bool dominoNavEdgeWeightIsPassable(domino_nav_weight_atom_t weight) {
    return (weight > 0 && weight < DOMINO_NAV_EDGE_WEIGHT_IMPASSABLE) != 0;
}

/**
 * @brief 添加或恢复一条有向 road，并在必要时创建起点/终点 fork road。
 *
 * 当传入 fork road 的 `id == 0` 时，engine 会分配新 ID 并把该 fork road 追加到全局数组。
 * road ID 由 `start_fork_road_id << 32 | target_fork_road_id` 组成。
 * 同 ID 道路已逻辑删除且两个端点仍活跃时，复用原道路记录并更新 weight[0]，保留 ID、几何及其余属性。
 * 同 ID 道路仍活跃时返回 ERR_ALREADY_EXISTS；已逻辑删除的端点不能通过本函数恢复。
 * 新增后分组的活跃路口数不得超过 MAX_FORK_ROAD_COUNT_PER_NETWORK，超限返回 ERR_OUT_OF_RANGE。
 *
 * @note id == 0 的参数须为调用方拥有的新实体；已登记实体的 ID 不得改写。
 * 添加会扩容 fork road / road 扁平数组；Nav 查询与修改须由调用方串行协调。
 * 调用后不得继续持有此前查询得到的实体指针，应按 ID 重新查询。
 *
 * @return `CODE_OK` 成功；失败返回具体负错误码。
 */
extern DOMINO_CODE dominoAddRoad(DominoForkRoad* start_fork_road, DominoForkRoad* target_fork_road, domino_nav_weight_atom_t weight,
                                 uint8_t region_index, uint8_t network_type);

/**
 * @brief 逻辑删除一条 road，并在 fork road 失去所有邻接关系时逻辑删除孤立 fork road。
 *
 * @return `CODE_OK` 成功；失败返回具体负错误码。
 */
extern DOMINO_CODE dominoRemoveRoad(domino_road_id_t road_id);

/**
 * @brief 更新道路权重（动态）。
 *
 * @warning 当前 shared 只保留声明，engine nav 模块尚未实现该函数。
 */
extern int dominoUpdateRoadWeight(domino_road_id_t road_id, float new_weight);

/**
 * @brief 阻塞或解阻塞道路（动态）。
 *
 * @warning 当前 shared 只保留声明，engine nav 模块尚未实现该函数。
 */
extern int dominoBlockRoad(domino_road_id_t road_id, bool blocked);
#endif
