#include "demo.h"

#include <time.h>

static void init_path_matrix(vertex_id_t vertex_num, const VertexEdges* vertices, PathInfo (*path_matrix)[vertex_num]) {
    for (vertex_id_t source_vertex_id = 0; source_vertex_id < vertex_num; source_vertex_id++) {
        PathInfo* const source_info = path_matrix[source_vertex_id];
        for (vertex_id_t target_vertex_id = 0; target_vertex_id < vertex_num; target_vertex_id++) {
            PathInfo* const path_info = &(source_info[target_vertex_id]);
            path_info->shortest_distance = WEIGHT_INFINITY;
            path_info->pre_vertex_id = PRE_VERTEX_NULL;
            path_info->pre_road_fork_id = 0;
        }
        PathInfo* const self_path_info = &(source_info[source_vertex_id]);
        self_path_info->shortest_distance = 0;

        const VertexEdges* const vert_edges = &vertices[source_vertex_id];
        for (int i = 0; i < vert_edges->count; i++) {
            const Edge* const edge = &(vert_edges->edges[i]);
            if (edge->weight > 0 && edge->weight < WEIGHT_INFINITY && edge->target_vertex_id != source_vertex_id) {
                PathInfo* const target_path_info = &(source_info[edge->target_vertex_id]);
                target_path_info->shortest_distance = edge->weight;
                target_path_info->pre_vertex_id = source_vertex_id;
                target_path_info->pre_road_fork_id = vert_edges->road_fork_id;
            }
        }
    }
}

static void floyd_core(vertex_id_t vertex_num, PathInfo (*path_matrix)[vertex_num], long* total_tick, long* hit_tick, long* null_weight_count,
                       long* null_weight_count_inner) {
    *total_tick = 0;
    *hit_tick = 0;
    *null_weight_count = 0;
    *null_weight_count_inner = 0;

    for (vertex_id_t middle_vertex_id = 0; middle_vertex_id < vertex_num; middle_vertex_id++) {
        PathInfo* const middle_info = path_matrix[middle_vertex_id];

        for (vertex_id_t source_vertex_id = 0; source_vertex_id < vertex_num; source_vertex_id++) {
            if (source_vertex_id == middle_vertex_id) {
                continue;
            }

            PathInfo* const source_info = path_matrix[source_vertex_id];
            const edge_weight_t source_to_middle = source_info[middle_vertex_id].shortest_distance;

            if (source_to_middle >= WEIGHT_INFINITY) {
                (*null_weight_count)++;
                continue;
            }

            for (vertex_id_t target_vertex_id = 0; target_vertex_id < vertex_num; target_vertex_id++) {
                (*total_tick)++;
                if (source_vertex_id == target_vertex_id || target_vertex_id == middle_vertex_id) {
                    continue;
                }

                const PathInfo* const middle_to_target_info = &(middle_info[target_vertex_id]);
                const edge_weight_t middle_to_target = middle_to_target_info->shortest_distance;

                if (middle_to_target >= WEIGHT_INFINITY) {
                    (*null_weight_count_inner)++;
                    continue;
                }

                const edge_weight_t new_weight = source_to_middle + middle_to_target;
                PathInfo* const source_to_target_info = &(source_info[target_vertex_id]);

                if (new_weight < source_to_target_info->shortest_distance) {
                    (*hit_tick)++;
                    source_to_target_info->shortest_distance = new_weight;
                    source_to_target_info->pre_vertex_id = middle_to_target_info->pre_vertex_id;
                    source_to_target_info->pre_road_fork_id = middle_to_target_info->pre_road_fork_id;
                }
            }
        }
    }
}

DOMINO_CODE findShortestPath(PathInfo* shortest_path_info, int road_fork_max_count, int* road_fork_count, vertex_id_t vertex_num,
                             PathInfo (*path_matrix)[vertex_num], vertex_id_t start_vertex_id, vertex_id_t end_vertex_id) {
    if (path_matrix[start_vertex_id][end_vertex_id].pre_vertex_id == PRE_VERTEX_NULL) {
        return ERR_NOT_FOUND;
    }
    // 使用栈来存储路径节点，避免递归
    vertex_id_t stack_top = 0;
    vertex_id_t current = end_vertex_id;
    vertex_id_t pre_vertex_id = PRE_VERTEX_NULL;

    do {
        PathInfo* current_info = &(path_matrix[start_vertex_id][current]);
        pre_vertex_id = current_info->pre_vertex_id;
        if (pre_vertex_id == start_vertex_id) {
            break;
        }
        if (stack_top >= road_fork_max_count) {
            return ERR_OUT_OF_RANGE;
        }
        shortest_path_info[stack_top] = *current_info;

        current = current_info->pre_vertex_id;
        stack_top++;
    } while (true);

    *road_fork_count = stack_top;
    return CODE_OK;
}

FloydPathPlanningResult floydPathPlanning(vertex_id_t vertex_num, const VertexEdges* vertices, PathInfo (*path_matrix)[vertex_num]) {
    FloydPathPlanningResult result = {0};

    clock_t start = clock();
    init_path_matrix(vertex_num, vertices, path_matrix);
    result.init_time_used = ((double)(clock() - start)) / CLOCKS_PER_SEC;

    long total_tick;
    long hit_tick;
    long null_weight_count;
    long null_weight_count_inner;
    start = clock();
    floyd_core(vertex_num, path_matrix, &total_tick, &hit_tick, &null_weight_count, &null_weight_count_inner);
    result.core_time_used = ((double)(clock() - start)) / CLOCKS_PER_SEC;
    result.total_tick = total_tick;
    result.hit_tick = hit_tick;
    result.null_weight_count = null_weight_count;
    result.null_weight_count_inner = null_weight_count_inner;

    return result;
}
