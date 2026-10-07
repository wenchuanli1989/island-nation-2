#ifndef DOMINO_ENGINE_NAV_GRAPH_H
#define DOMINO_ENGINE_NAV_GRAPH_H

#include <stdbool.h>

#include "domino_shared_error_codes.h"
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
 * @brief 一次道路扫描校验端点并收集邻接；失败保留原状态，成功后提交邻接、清空顶点索引并失效全部路径缓存。
 * @note 仅在加载后的基础校验通过后调用；Storage 已建立唯一 ID 映射，实体数组、map 与实体属性在调用期间不得修改。
 */
DOMINO_CODE navGraphRebuildAdjacency(void);

#endif
