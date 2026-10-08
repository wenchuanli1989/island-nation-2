#include "path.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../logger/entry.h"
#include "floyd.h"
#include "klib/kvec.h"
#include "road_id.h"
#include "state.h"
#include "storage_view.h"

static DOMINO_CODE navFillForkRoadListCache(const DominoNavDataSegment* segment, DominoForkRoad** fork_road_list_out) {
    if (!segment || !fork_road_list_out) {
        return ERR_INVALID_PARAM;
    }
    const uint32_t vertex_count_u32 = segment->total_count;
    if (vertex_count_u32 == 0U || vertex_count_u32 > MAX_FORK_ROAD_COUNT_PER_NETWORK) {
        return ERR_INVALID_PARAM;
    }
    const domino_nav_vertex_index_t vertex_count = (domino_nav_vertex_index_t)vertex_count_u32;

    const size_t fork_road_count = kv_size(domino_all_fork_road_list);
    if (fork_road_count > UINT32_MAX || segment->init_index > (uint32_t)fork_road_count) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navFillForkRoadListCache init_index out of range");
        return ERR_SYSTEM;
    }
    const uint64_t init_end_u64 = (uint64_t)segment->init_index + (uint64_t)segment->init_count;
    if (init_end_u64 > fork_road_count || init_end_u64 > UINT32_MAX) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navFillForkRoadListCache init end out of range");
        return ERR_SYSTEM;
    }
    if (segment->increment_index > (uint32_t)fork_road_count) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navFillForkRoadListCache increment_index out of range");
        return ERR_SYSTEM;
    }

    domino_nav_vertex_index_t filled_count = 0U;
    const uint8_t region_index = segment->region_index;
    const uint8_t road_network_type = segment->road_network_type;
    const uint32_t init_end = (uint32_t)init_end_u64;

    for (uint32_t index = segment->init_index; index < init_end; index++) {
        DominoForkRoad* fork_road = &kv_A(domino_all_fork_road_list, (size_t)index);
        if (fork_road->status != 0U) {
            continue;
        }
        if (fork_road->region_index != region_index || fork_road->type != road_network_type) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                              "fork road base entity at index=%u does not match its segment key", index);
            return ERR_INVALID_DATA;
        }
        if (filled_count >= vertex_count) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "fork road segment vertex overflow (capacity=%u)",
                              (uint32_t)vertex_count);
            return ERR_SYSTEM;
        }
        fork_road_list_out[filled_count] = fork_road;
        fork_road->temp_index = filled_count;
        filled_count++;
    }

    for (uint32_t index = segment->increment_index; index < (uint32_t)fork_road_count; index++) {
        DominoForkRoad* fork_road = &kv_A(domino_all_fork_road_list, (size_t)index);
        if (fork_road->status != 0U || fork_road->region_index != region_index || fork_road->type != road_network_type) {
            continue;
        }
        if (filled_count >= vertex_count) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "fork road segment vertex overflow (capacity=%u)",
                              (uint32_t)vertex_count);
            return ERR_SYSTEM;
        }
        fork_road_list_out[filled_count] = fork_road;
        fork_road->temp_index = filled_count;
        filled_count++;
    }

    if (filled_count != vertex_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "fork road segment vertex count mismatch: expected=%u actual=%u",
                          (uint32_t)vertex_count, (uint32_t)filled_count);
        return ERR_SYSTEM;
    }
    return CODE_OK;
}

static DominoForkRoad* navFindNearestForkRoad(domino_nav_vertex_index_t vertex_count, DominoForkRoad* const* fork_road_list,
                                              DominoLocationXY location) {
    if (vertex_count == 0U || !fork_road_list) {
        return nullptr;
    }

    DominoForkRoad* nearest_fork_road = nullptr;
    uint64_t nearest_distance_squared_low = 0U;
    bool nearest_distance_squared_high = false;
    for (domino_nav_vertex_index_t vertex_index = 0U; vertex_index < vertex_count; vertex_index++) {
        DominoForkRoad* fork_road = fork_road_list[vertex_index];
        if (!fork_road) {
            continue;
        }
        const int64_t delta_x = (int64_t)fork_road->position_x - (int64_t)location.x;
        const int64_t delta_y = (int64_t)fork_road->position_y - (int64_t)location.y;
        const uint64_t absolute_delta_x = delta_x < 0 ? (uint64_t)-delta_x : (uint64_t)delta_x;
        const uint64_t absolute_delta_y = delta_y < 0 ? (uint64_t)-delta_y : (uint64_t)delta_y;
        const uint64_t delta_x_squared = absolute_delta_x * absolute_delta_x;
        const uint64_t delta_y_squared = absolute_delta_y * absolute_delta_y;
        /* 两个 32 位坐标差的平方和最多需要 65 位，单独保留加法进位。 */
        const uint64_t distance_squared_low = delta_x_squared + delta_y_squared;
        const bool distance_squared_high = distance_squared_low < delta_x_squared;
        if (!nearest_fork_road || distance_squared_high < nearest_distance_squared_high ||
            (distance_squared_high == nearest_distance_squared_high && distance_squared_low < nearest_distance_squared_low)) {
            nearest_fork_road = fork_road;
            nearest_distance_squared_low = distance_squared_low;
            nearest_distance_squared_high = distance_squared_high;
        }
    }
    return nearest_fork_road;
}

static DOMINO_CODE navFillRoadLinePath(DominoRoadLine* road_line, domino_nav_vertex_index_t vertex_count, DominoForkRoad* const* fork_road_list,
                                       PathInfo (*path_matrix)[vertex_count], domino_nav_vertex_index_t start_vertex_index,
                                       domino_nav_vertex_index_t target_vertex_index) {
    if (!road_line || vertex_count == 0U || !fork_road_list || !path_matrix) {
        return ERR_INVALID_PARAM;
    }
    if (start_vertex_index >= vertex_count || target_vertex_index >= vertex_count) {
        return ERR_OUT_OF_RANGE;
    }

    for (uint32_t index = 0U; index < MAX_FORK_ROAD_COUNT_PER_ROAD_LINE; index++) {
        road_line->fork_road_id_list[index] = 0U;
    }
    road_line->fork_road_count = 0U;

    if (start_vertex_index == target_vertex_index) {
        road_line->remaining_distance = 0U;
        return CODE_OK;
    }

    PathInfo shortest_path_info[MAX_FORK_ROAD_COUNT_PER_ROAD_LINE];
    int shortest_path_info_count = 0;
    DOMINO_CODE result = findShortestPath(shortest_path_info, MAX_FORK_ROAD_COUNT_PER_ROAD_LINE, &shortest_path_info_count, vertex_count, path_matrix,
                                          start_vertex_index, target_vertex_index);
    if (result != CODE_OK) {
        return result;
    }

    road_line->remaining_distance = path_matrix[start_vertex_index][target_vertex_index].min_weight;

    /* 回溯结果按终点到起点排列，只翻转中间节点，不包含起终点。 */
    for (int index = 0; index < shortest_path_info_count; index++) {
        domino_nav_vertex_index_t previous_vertex_index = shortest_path_info[shortest_path_info_count - 1 - index].pre_vertex_index;
        road_line->fork_road_id_list[index] = fork_road_list[previous_vertex_index]->id;
    }
    road_line->fork_road_count = (uint16_t)shortest_path_info_count;
    return CODE_OK;
}

DOMINO_CODE dominoNavigation(DominoRoadLine* road_line) {
    if (!road_line) {
        return ERR_NULL_POINTER;
    }

    const uint8_t region_index = road_line->region_index;
    const uint8_t road_network_type = road_line->type;
    const DominoNavDataSegment* segment = navStateFindSegment(&domino_all_fork_road_nav_data_segment_list, region_index, road_network_type);
    if (!segment || segment->total_count == 0U) {
        return ERR_NOT_FOUND;
    }

    const DominoNavPathCache* cache = navStateGetPathCache(region_index, road_network_type);
    if (!cache) {
        DOMINO_CODE result = dominoNavPathPlanning(region_index, road_network_type, nullptr);
        if (result != CODE_OK) {
            return result;
        }
        cache = navStateGetPathCache(region_index, road_network_type);
        if (!cache) {
            return ERR_GAME_STATE_INVALID;
        }
    }

    const domino_nav_vertex_index_t vertex_count = cache->vertex_count;
    DominoForkRoad* const* fork_road_list = cache->fork_road_list;
    DominoForkRoad* start_fork_road = nullptr;
    DominoForkRoad* target_fork_road = nullptr;

    if (road_line->current_fork_road_id != 0U) {
        start_fork_road = dominoGetForkRoadByID(road_line->current_fork_road_id);
        if (!start_fork_road || start_fork_road->status != 0U || start_fork_road->region_index != region_index ||
            start_fork_road->type != road_network_type) {
            DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                                  "navigation start fork is missing or outside the requested network");
            return ERR_NOT_FOUND;
        }
    } else if (!dominoLocationXYIsNull(road_line->current_location)) {
        start_fork_road = navFindNearestForkRoad(vertex_count, fork_road_list, road_line->current_location);
        if (!start_fork_road) {
            return ERR_NOT_FOUND;
        }
        road_line->current_fork_road_id = start_fork_road->id;
    } else {
        if (dominoLocationXYIsNull(road_line->start_location) || dominoLocationXYIsNull(road_line->target_location)) {
            return ERR_INVALID_PARAM;
        }
        start_fork_road = navFindNearestForkRoad(vertex_count, fork_road_list, road_line->start_location);
        target_fork_road = navFindNearestForkRoad(vertex_count, fork_road_list, road_line->target_location);
        if (!start_fork_road || !target_fork_road) {
            return ERR_NOT_FOUND;
        }

        road_line->target_fork_road_id = target_fork_road->id;
        road_line->start_fork_road_id = start_fork_road->id;
        road_line->current_fork_road_id = start_fork_road->id;
        road_line->current_location = road_line->start_location;
        road_line->region_index = region_index;
        road_line->type = road_network_type;
        road_line->status = 0U;
    }

    if (!target_fork_road && road_line->target_fork_road_id != 0U) {
        target_fork_road = dominoGetForkRoadByID(road_line->target_fork_road_id);
    }
    if (!target_fork_road && !dominoLocationXYIsNull(road_line->target_location)) {
        target_fork_road = navFindNearestForkRoad(vertex_count, fork_road_list, road_line->target_location);
        if (target_fork_road) {
            road_line->target_fork_road_id = target_fork_road->id;
        }
    }
    if (!target_fork_road || target_fork_road->status != 0U || target_fork_road->region_index != region_index ||
        target_fork_road->type != road_network_type) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                              "navigation target fork is missing or outside the requested network");
        return ERR_NOT_FOUND;
    }

    PathInfo(*path_matrix)[vertex_count] = (PathInfo(*)[vertex_count])cache->path_matrix;
    DOMINO_CODE result =
        navFillRoadLinePath(road_line, vertex_count, fork_road_list, path_matrix, start_fork_road->temp_index, target_fork_road->temp_index);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "failed to fill road line path (code=%d)", result);
    }
    return result;
}

DOMINO_CODE dominoNavPathPlanning(uint8_t region_index, uint8_t road_network_type, FloydPathPlanningResult* out_stat) {
    const DominoNavDataSegment* segment = navStateFindSegment(&domino_all_fork_road_nav_data_segment_list, region_index, road_network_type);
    if (!segment) {
        return ERR_NOT_FOUND;
    }

    const uint32_t vertex_count_u32 = segment->total_count;
    if (vertex_count_u32 == 0U) {
        navStateInvalidatePathCache(region_index, road_network_type);
        if (out_stat) {
            *out_stat = (FloydPathPlanningResult){0};
        }
        return CODE_OK;
    }
    if (vertex_count_u32 > MAX_FORK_ROAD_COUNT_PER_NETWORK) {
        return ERR_OUT_OF_RANGE;
    }
    const domino_nav_vertex_index_t vertex_count = (domino_nav_vertex_index_t)vertex_count_u32;

    DominoNavPathCache* cache = navStatePreparePathCache(region_index, road_network_type);
    if (!cache) {
        return ERR_MEMORY_ALLOC;
    }

    DominoForkRoad** fork_road_list = (DominoForkRoad**)calloc((size_t)vertex_count, sizeof(*fork_road_list));
    if (!fork_road_list) {
        return ERR_MEMORY_ALLOC;
    }
    DOMINO_CODE result = navFillForkRoadListCache(segment, fork_road_list);
    if (result != CODE_OK) {
        free(fork_road_list);
        return result;
    }

    VertexEdges* vertices = (VertexEdges*)calloc((size_t)vertex_count, sizeof(*vertices));
    if (!vertices) {
        free(fork_road_list);
        return ERR_MEMORY_ALLOC;
    }

    for (size_t vertex_index = 0U; vertex_index < (size_t)vertex_count; vertex_index++) {
        DominoForkRoad* from_fork_road = fork_road_list[vertex_index];
        VertexEdges* vertex_edges = &vertices[vertex_index];
        vertex_edges->road_fork_id = from_fork_road->id;
        for (uint8_t edge_index = 0U; edge_index < from_fork_road->to_fork_road_count && edge_index < MAX_EDGES_PER_VERTEX; edge_index++) {
            const domino_fork_road_id_t target_id = from_fork_road->to_fork_road_id[edge_index];
            if (target_id == 0U || target_id == from_fork_road->id) {
                continue;
            }
            DominoForkRoad* target_fork_road = dominoGetForkRoadByID(target_id);
            if (!target_fork_road || target_fork_road->status != 0U || target_fork_road->region_index != region_index ||
                target_fork_road->type != road_network_type) {
                continue;
            }
            DominoRoad* road = dominoGetRoadByID(navBuildRoadId(from_fork_road->id, target_id));
            if (!road || road->status != 0U || !dominoNavEdgeWeightIsPassable(road->weight[0])) {
                continue;
            }
            vertex_edges->edges[vertex_edges->count].target_vertex_index = target_fork_road->temp_index;
            vertex_edges->edges[vertex_edges->count].weight = road->weight[0];
            vertex_edges->count++;
        }
    }

    const size_t matrix_element_count = (size_t)vertex_count * (size_t)vertex_count;
    PathInfo* path_matrix = (PathInfo*)calloc(matrix_element_count, sizeof(*path_matrix));
    if (!path_matrix) {
        free(vertices);
        free(fork_road_list);
        return ERR_MEMORY_ALLOC;
    }

    FloydPathPlanningResult stat = floydPathPlanning(vertex_count, vertices, (PathInfo(*)[vertex_count])path_matrix);
    free(vertices);

    navStateCommitPathCache(cache, path_matrix, fork_road_list, vertex_count);

    if (out_stat) {
        *out_stat = stat;
    }
    return CODE_OK;
}
