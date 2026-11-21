#ifndef DOMINO_SHARED_NAV_H
#define DOMINO_SHARED_NAV_H

#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

/** @file domino_shared_nav.h
 * @brief 导航实体（路口 fork road、有向道路 road、路线 road line）和道路增删接口。
 * @note engine nav 按 (region_index, road_network_type) 对路口和道路分组，每组按需维护 Floyd 路径缓存。
 */

#define DOMINO_MAX_EDGE_COUNT_PER_VERTEX 9
#define DOMINO_ROAD_INLINE_CORNER_POINT_MAX 8  ///< 道路内联保存的拐点上限，超出时改用堆数组。
#define DOMINO_MAX_FORK_ROAD_COUNT_PER_ROAD_LINE 14
#define DOMINO_MAX_FORK_ROAD_COUNT_PER_NETWORK 3000  ///< 每个 (region_index, network_type) 分组的活跃路口上限。

#define DOMINO_NAV_EDGE_WEIGHT_IMPASSABLE UINT8_MAX  ///< 不可通行的边权；0 同样不可通行。

typedef uint32_t domino_nav_weight_t;      ///< 路径权重之和。
typedef uint8_t domino_nav_edge_weight_t;  ///< 单条道路的权重。
typedef uint16_t domino_nav_vertex_index_t;
static_assert(DOMINO_MAX_FORK_ROAD_COUNT_PER_NETWORK <= UINT16_MAX, "network vertex limit must fit domino_nav_vertex_index_t");

/** @brief 路网类型位标记；实体的 type 字段保存该值或调用方约定的类型。 */
typedef enum {
    DOMINO_ROAD_NETWORK_ROAD = 0b00000001,
    DOMINO_ROAD_NETWORK_RAILWAY = 0b00000010,
    DOMINO_ROAD_NETWORK_WATERWAY = 0b00000100,
    DOMINO_ROAD_NETWORK_AIRWAY = 0b00001000,
    DOMINO_ROAD_NETWORK_HIGHWAY = 0b00010000,
} DOMINO_ROAD_NETWORK;

typedef struct DominoForkRoad {
    domino_fork_road_id_t id;

    int32_t position_x;
    int32_t position_y;
    int32_t position_z;

    domino_fork_road_id_t to_fork_road_id_list[DOMINO_MAX_EDGE_COUNT_PER_VERTEX];  ///< 出边终点；加载后由道路重建，不持久化。

    domino_nav_vertex_index_t vertex_index;  ///< 在本组路径缓存中的顶点下标；不持久化。

    domino_nav_edge_weight_t weight;  ///< 节点权重；寻路目前只用 road.weight[0]。
    uint8_t region_index;
    uint8_t to_fork_road_count;
    domino_status_t status;  ///< 0 为活跃，非 0 为逻辑删除。

    domino_type_t type;     ///< 路网分组类型。
    uint8_t num[2];         ///< 固定两个字节，不是 C 字符串。
    uint8_t other_info[3];  ///< 预留，不持久化。
} DominoForkRoad;
static_assert(sizeof(DominoForkRoad) == 64, "DominoForkRoad must be exactly 64 bytes");

typedef struct DominoRoad {
    /* 拐点数不超过内联上限时用内联数组 [轴][点]，否则用堆数组 [点][轴]。 */
    union {
        int32_t (*corner_point_ptr)[3];
        int32_t corner_points[3][DOMINO_ROAD_INLINE_CORNER_POINT_MAX];
    };
    domino_road_id_t id;  ///< 高 32 位为起点 ID，低 32 位为终点 ID。

    uint32_t distance;
    uint16_t corner_point_count;

    uint8_t time[4];
    uint8_t cost;
    uint8_t speed[4];

    domino_nav_edge_weight_t weight[4];  ///< 寻路只用 weight[0]；0 和 UINT8_MAX 不可通行。

    domino_type_t type;      ///< 路网分组类型。
    domino_status_t status;  ///< 0 为活跃，非 0 为逻辑删除。
    uint8_t region_index;
    uint8_t num[2];  ///< 固定两个字节，不是 C 字符串。
} DominoRoad;
static_assert(sizeof(DominoRoad) == 128, "DominoRoad must be exactly 128 bytes");

/** @brief 路线：起点到终点经过的路口序列，由 dominoNavRouteRoadLine() 根据起点、当前位置和终点填充。 */
typedef struct DominoRoadLine {
    domino_road_line_id_t id;
    domino_fork_road_id_t fork_road_id_list[DOMINO_MAX_FORK_ROAD_COUNT_PER_ROAD_LINE];  ///< 按行进顺序排列的中间节点，不含起终点。

    DominoLocationXY start_location;
    DominoLocationXY target_location;
    DominoLocationXY current_location;
    domino_fork_road_id_t start_fork_road_id;
    domino_fork_road_id_t target_fork_road_id;
    domino_fork_road_id_t current_fork_road_id;

    domino_runtime_ms_t elapsed_ms;
    domino_runtime_ms_t remaining_ms;
    uint32_t passed_distance;
    uint32_t remaining_distance;  ///< 目前写入最短路径权重，尚未换算为物理距离。
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

static inline bool dominoNavEdgeWeightIsPassable(domino_nav_edge_weight_t weight) {
    return weight > 0 && weight < DOMINO_NAV_EDGE_WEIGHT_IMPASSABLE;
}

/** @brief 添加一条有向道路；端点 id 为 0 时分配新 ID 并新建该路口，回写 ID、region_index 和 type。
 * @note 道路 ID 为 (起点 ID << 32 | 终点 ID)。同 ID 道路已逻辑删除而两端仍活跃时，恢复原记录并只更新 weight[0]。
 * 增删会扩容实体数组：调用后之前取得的实体指针失效，须按 ID 重新查询。Nav 的查询与修改由调用方串行协调。
 * @return CODE_OK 成功；端点不存在 ERR_NOT_FOUND；端点已删除 ERR_GAME_STATE_INVALID；参数或分组不符 ERR_INVALID_PARAM；
 *         道路已存在 ERR_ALREADY_EXISTS；出度或分组路口数超限 ERR_CAPACITY_EXCEEDED。结果由调用方记录，Nav 内部不打印。
 * @note 未初始化、数据损坏、分配失败或 ID 耗尽致命退出。
 */
extern DOMINO_CODE dominoAddRoad(DominoForkRoad* start_fork_road, DominoForkRoad* target_fork_road, domino_nav_edge_weight_t weight,
                                 uint8_t region_index, uint8_t network_type);

/** @brief 逻辑删除一条道路；端点因此不再连接任何活跃道路时一并逻辑删除。
 * @return CODE_OK 成功；不存在 ERR_NOT_FOUND；已删除 ERR_GAME_STATE_INVALID。未初始化或数据损坏致命退出。
 */
extern DOMINO_CODE dominoRemoveRoad(domino_road_id_t road_id);

/** @brief 动态更新道路权重。尚未实现，只有声明。 */
extern int dominoUpdateRoadWeight(domino_road_id_t road_id, float new_weight);

/** @brief 阻塞或解除阻塞道路。尚未实现，只有声明。 */
extern int dominoBlockRoad(domino_road_id_t road_id, bool blocked);
#endif
