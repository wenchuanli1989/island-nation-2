#ifndef DOMINO_ENGINE_NAV_FLOYD_H
#define DOMINO_ENGINE_NAV_FLOYD_H

#include "domino_shared_error_codes.h"
#include "model_view.h"

/**
 * @brief 反向回溯最短路径，输出各中间节点对应的前驱记录，不包含起终点。
 * @return 不可达返回 ERR_NOT_FOUND，输出容量不足返回 ERR_OUT_OF_RANGE；成功时写入记录数。
 * @note 调用方保证矩阵由 Floyd 构建且两个顶点下标有效；同点路径由调用方处理。
 */
extern DOMINO_CODE findShortestPath(PathInfo* shortest_path_info, int max_shortest_path_info_count, int* shortest_path_info_count,
                                    domino_nav_vertex_index_t vertex_num, PathInfo (*path_matrix)[vertex_num],
                                    domino_nav_vertex_index_t start_vertex_index, domino_nav_vertex_index_t end_vertex_index);

/** @brief 构建全点对最短路径；耗时统计来自 clock()，单位秒。 */
extern FloydPathPlanningResult floydPathPlanning(domino_nav_vertex_index_t vertex_num, const VertexEdges* vertices,
                                                 PathInfo (*path_matrix)[vertex_num]);
#endif
