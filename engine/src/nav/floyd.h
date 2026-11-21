#ifndef DOMINO_ENGINE_NAV_FLOYD_H
#define DOMINO_ENGINE_NAV_FLOYD_H

#include "domino_shared_error_codes.h"
#include "types.h"

/** @brief 用 Floyd-Warshall 填充全点对最短路径矩阵（vertex_count × vertex_count）。
 * @return 耗时和松弛计数。
 */
extern DominoNavFloydStats navFloydBuildPaths(domino_nav_vertex_index_t vertex_count, const DominoNavVertexEdges* vertex_list,
                                              DominoNavPathCell (*path_matrix)[vertex_count]);

/** @brief 从终点沿前驱回溯到起点，按终点到起点的顺序输出中间顶点的矩阵单元，不含起终点。
 * @pre 矩阵由 navFloydBuildPaths 构建，两个顶点下标有效且不相同。
 * @return CODE_OK 并写入 out_step_count；不可达 ERR_NOT_FOUND；中间顶点超过 step_capacity 时 ERR_BUFFER_TOO_SMALL。
 */
extern DOMINO_CODE navFloydTracePath(DominoNavPathCell* out_step_list, int step_capacity, int* out_step_count, domino_nav_vertex_index_t vertex_count,
                                     DominoNavPathCell (*path_matrix)[vertex_count], domino_nav_vertex_index_t start_vertex_index,
                                     domino_nav_vertex_index_t end_vertex_index);
#endif
