#ifndef DOMINO_ENGINE_NAV_GRAPH_H
#define DOMINO_ENGINE_NAV_GRAPH_H

#include <stdbool.h>

#include "domino_shared_nav.h"

/** @file graph.h
 * @brief 路口出边（邻接表）操作。调用方保证路口有效、目标 ID 非零，出度不超过 DOMINO_MAX_EDGE_COUNT_PER_VERTEX。
 */

bool navGraphHasEdge(const DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id);

/** @pre 边不存在且出度未满。 */
void navGraphAppendEdge(DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id);

/** @brief 删除出边，保持其余出边顺序；未找到返回 false。 */
bool navGraphRemoveEdge(DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id);

/** @brief 清空全部邻接和顶点下标，按活跃且可通行的道路重建，并失效全部路径缓存。出度超限致命退出。 */
void navGraphRebuildAdjacency(void);

#endif
