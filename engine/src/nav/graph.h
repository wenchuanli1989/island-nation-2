#ifndef DOMINO_ENGINE_NAV_GRAPH_H
#define DOMINO_ENGINE_NAV_GRAPH_H

#include <stdbool.h>

#include "domino_shared_nav.h"

/**
 * @brief 查询有向出边是否存在。
 * @note 调用方须保证起点有效、目标 ID 非零，并且出度不超过 MAX_EDGES_PER_VERTEX。
 */
bool navGraphHasEdge(const DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id);

/**
 * @brief 直接追加有向出边。
 * @note 调用方须保证起点有效、目标 ID 非零且对应有效节点、边不存在，并且出度小于 MAX_EDGES_PER_VERTEX。
 */
void navGraphAppendEdge(DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id);

/**
 * @brief 删除有向出边；未找到时返回 false。
 * @note 调用方须保证起点有效、目标 ID 非零且对应有效节点，并且出度不超过 MAX_EDGES_PER_VERTEX。
 */
bool navGraphRemoveEdge(DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id);

/**
 * @brief 清空邻接和顶点索引后按道路重建邻接，并失效全部路径缓存。
 * @pre Storage 已恢复当前格式的正确世界及唯一 ID map，Nav 容量已检查，调用期间实体数组和属性保持稳定。
 * @note 不重复复核道路关系；追加邻接仍检查固定出度容量，超限直接打印到 stderr 并 exit。
 */
void navGraphRebuildAdjacency(void);

#endif
