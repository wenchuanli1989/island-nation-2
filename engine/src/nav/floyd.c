#include "floyd.h"

#include <time.h>

static void initPathMatrix(domino_nav_vertex_index_t vertex_num, const VertexEdges* vertices, PathInfo (*path_matrix)[vertex_num]) {
    for (uint16_t source_vertex_index = 0; source_vertex_index < vertex_num; source_vertex_index++) {
        PathInfo* const source_info = path_matrix[source_vertex_index];
        for (uint16_t target_vertex_index = 0; target_vertex_index < vertex_num; target_vertex_index++) {
            PathInfo* const path_info = &(source_info[target_vertex_index]);
            path_info->min_weight = WEIGHT_INFINITY;
            path_info->pre_vertex_index = PRE_VERTEX_NULL;
            path_info->pre_road_fork_id = 0;
        }
        PathInfo* const self_path_info = &(source_info[source_vertex_index]);
        self_path_info->min_weight = 0;

        const VertexEdges* const vert_edges = &vertices[source_vertex_index];
        for (int i = 0; i < vert_edges->count; i++) {
            const Edge* const edge = &(vert_edges->edges[i]);
            PathInfo* const target_path_info = &(source_info[edge->target_vertex_index]);
            target_path_info->min_weight = edge->weight;
            target_path_info->pre_vertex_index = source_vertex_index;
            target_path_info->pre_road_fork_id = vert_edges->road_fork_id;
        }
    }
}

static void floydCore(domino_nav_vertex_index_t vertex_num, PathInfo (*path_matrix)[vertex_num], FloydPathPlanningResult* result) {
    /* 中间顶点必须位于最外层，逐轮扩大允许经过的顶点集合。 */
    for (domino_nav_vertex_index_t middle_vertex_index = 0; middle_vertex_index < vertex_num; middle_vertex_index++) {
        PathInfo* const middle_info = path_matrix[middle_vertex_index];

        for (domino_nav_vertex_index_t source_vertex_index = 0; source_vertex_index < vertex_num; source_vertex_index++) {
            if (source_vertex_index == middle_vertex_index) {
                continue;
            }

            PathInfo* const source_info = path_matrix[source_vertex_index];
            const domino_nav_weight_t source_to_middle = source_info[middle_vertex_index].min_weight;

            if (source_to_middle >= WEIGHT_INFINITY) {
                result->null_weight_count++;
                continue;
            }

            for (domino_nav_vertex_index_t target_vertex_index = 0; target_vertex_index < vertex_num; target_vertex_index++) {
                result->total_tick++;
                if (source_vertex_index == target_vertex_index || target_vertex_index == middle_vertex_index) {
                    continue;
                }

                const PathInfo* const middle_to_target_info = &(middle_info[target_vertex_index]);
                const domino_nav_weight_t middle_to_target = middle_to_target_info->min_weight;

                if (middle_to_target >= WEIGHT_INFINITY) {
                    result->null_weight_count_inner++;
                    continue;
                }

                const domino_nav_weight_t new_weight = source_to_middle + middle_to_target;
                PathInfo* const source_to_target_info = &(source_info[target_vertex_index]);

                if (new_weight < source_to_target_info->min_weight) {
                    result->hit_tick++;
                    source_to_target_info->min_weight = new_weight;
                    source_to_target_info->pre_vertex_index = middle_to_target_info->pre_vertex_index;
                    source_to_target_info->pre_road_fork_id = middle_to_target_info->pre_road_fork_id;
                }
            }
        }
    }
}

DOMINO_CODE findShortestPath(PathInfo* shortest_path_info, int max_shortest_path_info_count, int* shortest_path_info_count,
                             domino_nav_vertex_index_t vertex_num, PathInfo (*path_matrix)[vertex_num], domino_nav_vertex_index_t start_vertex_index,
                             domino_nav_vertex_index_t end_vertex_index) {
    if (path_matrix[start_vertex_index][end_vertex_index].pre_vertex_index == PRE_VERTEX_NULL) {
        return ERR_NOT_FOUND;
    }
    /* 沿起点所在行反向回溯；每项的前驱是一个中间节点。 */
    domino_nav_vertex_index_t stack_top = 0;
    domino_nav_vertex_index_t current = end_vertex_index;
    do {
        PathInfo* current_info = &(path_matrix[start_vertex_index][current]);
        if (current_info->pre_vertex_index == start_vertex_index) {
            break;
        }
        if (stack_top >= max_shortest_path_info_count) {
            return ERR_OUT_OF_RANGE;
        }
        shortest_path_info[stack_top] = *current_info;

        current = current_info->pre_vertex_index;
        stack_top++;
    } while (true);

    *shortest_path_info_count = stack_top;
    return CODE_OK;
}

FloydPathPlanningResult floydPathPlanning(domino_nav_vertex_index_t vertex_num, const VertexEdges* vertices, PathInfo (*path_matrix)[vertex_num]) {
    FloydPathPlanningResult result = {0};

    clock_t start = clock();
    initPathMatrix(vertex_num, vertices, path_matrix);
    result.init_time_used = ((double)(clock() - start)) / CLOCKS_PER_SEC;

    start = clock();
    floydCore(vertex_num, path_matrix, &result);
    result.core_time_used = ((double)(clock() - start)) / CLOCKS_PER_SEC;

    return result;
}
