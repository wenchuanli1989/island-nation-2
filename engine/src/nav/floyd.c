#include "floyd.h"

#include <assert.h>
#include <time.h>

/** @brief 自身距离为 0，直接出边取边权并以起点为前驱，其余不可达。 */
static void initPathMatrix(domino_nav_vertex_index_t vertex_count, const DominoNavVertexEdges* vertex_list,
                           DominoNavPathCell (*path_matrix)[vertex_count]) {
    for (domino_nav_vertex_index_t source_index = 0; source_index < vertex_count; source_index++) {
        DominoNavPathCell* const source_row = path_matrix[source_index];
        for (domino_nav_vertex_index_t target_index = 0; target_index < vertex_count; target_index++) {
            DominoNavPathCell* const cell_ptr = &source_row[target_index];
            cell_ptr->min_weight = DOMINO_NAV_WEIGHT_INFINITY;
            cell_ptr->pre_vertex_index = DOMINO_NAV_VERTEX_NONE;
            cell_ptr->pre_fork_road_id = 0;
        }
        source_row[source_index].min_weight = 0;

        const DominoNavVertexEdges* const vertex_ptr = &vertex_list[source_index];
        for (uint8_t edge_index = 0; edge_index < vertex_ptr->edge_count; edge_index++) {
            const DominoNavEdge* const edge_ptr = &vertex_ptr->edge_list[edge_index];
            DominoNavPathCell* const cell_ptr = &source_row[edge_ptr->target_vertex_index];
            cell_ptr->min_weight = edge_ptr->weight;
            cell_ptr->pre_vertex_index = source_index;
            cell_ptr->pre_fork_road_id = vertex_ptr->fork_road_id;
        }
    }
}

static void relaxAllPairs(domino_nav_vertex_index_t vertex_count, DominoNavPathCell (*path_matrix)[vertex_count], DominoNavFloydStats* stats_ptr) {
    // 中间顶点必须在最外层循环：第 k 轮允许经过前 k 个顶点。
    for (domino_nav_vertex_index_t middle_index = 0; middle_index < vertex_count; middle_index++) {
        DominoNavPathCell* const middle_row = path_matrix[middle_index];
        for (domino_nav_vertex_index_t source_index = 0; source_index < vertex_count; source_index++) {
            if (source_index == middle_index) {
                continue;
            }
            DominoNavPathCell* const source_row = path_matrix[source_index];
            const domino_nav_weight_t source_to_middle = source_row[middle_index].min_weight;
            if (source_to_middle >= DOMINO_NAV_WEIGHT_INFINITY) {
                stats_ptr->unreachable_source_count++;
                continue;
            }
            for (domino_nav_vertex_index_t target_index = 0; target_index < vertex_count; target_index++) {
                stats_ptr->relax_check_count++;
                if (source_index == target_index || target_index == middle_index) {
                    continue;
                }
                const DominoNavPathCell* const middle_to_target_ptr = &middle_row[target_index];
                if (middle_to_target_ptr->min_weight >= DOMINO_NAV_WEIGHT_INFINITY) {
                    stats_ptr->unreachable_target_count++;
                    continue;
                }
                const domino_nav_weight_t new_weight = source_to_middle + middle_to_target_ptr->min_weight;
                DominoNavPathCell* const source_to_target_ptr = &source_row[target_index];
                if (new_weight < source_to_target_ptr->min_weight) {
                    // 经过中间顶点更短：终点的前驱沿用“中间点 → 终点”路径上的前驱。
                    stats_ptr->relax_hit_count++;
                    source_to_target_ptr->min_weight = new_weight;
                    source_to_target_ptr->pre_vertex_index = middle_to_target_ptr->pre_vertex_index;
                    source_to_target_ptr->pre_fork_road_id = middle_to_target_ptr->pre_fork_road_id;
                }
            }
        }
    }
}

DOMINO_CODE navFloydTracePath(DominoNavPathCell* out_step_list, int step_capacity, int* out_step_count, domino_nav_vertex_index_t vertex_count,
                              DominoNavPathCell (*path_matrix)[vertex_count], domino_nav_vertex_index_t start_vertex_index,
                              domino_nav_vertex_index_t end_vertex_index) {
    if (path_matrix[start_vertex_index][end_vertex_index].pre_vertex_index == DOMINO_NAV_VERTEX_NONE) {
        return ERR_NOT_FOUND;
    }
    // 沿起点所在行从终点反向回溯，每一步的前驱都是一个中间顶点，回到起点为止。
    int step_count = 0;
    domino_nav_vertex_index_t current_index = end_vertex_index;
    while (true) {
        const DominoNavPathCell* cell_ptr = &path_matrix[start_vertex_index][current_index];
        assert(cell_ptr->pre_vertex_index < vertex_count && cell_ptr->pre_vertex_index != current_index && step_count < vertex_count);
        if (cell_ptr->pre_vertex_index == start_vertex_index) {
            break;
        }
        if (step_count >= step_capacity) {
            return ERR_BUFFER_TOO_SMALL;
        }
        out_step_list[step_count++] = *cell_ptr;
        current_index = cell_ptr->pre_vertex_index;
    }
    *out_step_count = step_count;
    return CODE_OK;
}

DominoNavFloydStats navFloydBuildPaths(domino_nav_vertex_index_t vertex_count, const DominoNavVertexEdges* vertex_list,
                                       DominoNavPathCell (*path_matrix)[vertex_count]) {
    DominoNavFloydStats stats = {0};

    clock_t start_clock = clock();
    initPathMatrix(vertex_count, vertex_list, path_matrix);
    stats.init_time_s = ((double)(clock() - start_clock)) / CLOCKS_PER_SEC;

    start_clock = clock();
    relaxAllPairs(vertex_count, path_matrix, &stats);
    stats.core_time_s = ((double)(clock() - start_clock)) / CLOCKS_PER_SEC;

    return stats;
}
