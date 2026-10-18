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

/** @brief 调用方已定位分段；规划期间实体数组和分段保持稳定。 */
static DOMINO_CODE navPlanSegmentPaths(const DominoNavDataSegment* segment, FloydPathPlanningResult* out_stat);

/** @brief 分段非空、顶点数已校验且输出数组已分配；实体属性和填充数量仍须校验。 */
static DOMINO_CODE navFillForkRoadListCache(const DominoNavDataSegment* segment, DominoForkRoad** fork_road_list_out) {
    const domino_nav_vertex_index_t vertex_count = (domino_nav_vertex_index_t)segment->total_count;
    const size_t fork_road_count = kv_size(domino_all_fork_road_list);
    /* 加载与新增入口已限制实体数；64 位加法同时覆盖基础段起点和终点检查。 */
    const uint64_t init_end_u64 = (uint64_t)segment->init_index + (uint64_t)segment->init_count;
    if (init_end_u64 > fork_road_count) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navFillForkRoadListCache init end out of range");
        return ERR_INVALID_DATA;
    }
    if (segment->increment_index > (uint32_t)fork_road_count) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navFillForkRoadListCache increment_index out of range");
        return ERR_INVALID_DATA;
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
            return ERR_INVALID_DATA;
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
            return ERR_INVALID_DATA;
        }
        fork_road_list_out[filled_count] = fork_road;
        fork_road->temp_index = filled_count;
        filled_count++;
    }

    if (filled_count != vertex_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "fork road segment vertex count mismatch: expected=%u actual=%u",
                          (uint32_t)vertex_count, (uint32_t)filled_count);
        return ERR_INVALID_DATA;
    }
    return CODE_OK;
}

/** @brief 完整路径缓存保证顶点数非零、表内各实体指针有效；返回最近的实体。 */
static DominoForkRoad* navFindNearestForkRoad(domino_nav_vertex_index_t vertex_count, DominoForkRoad* const* fork_road_list,
                                              DominoLocationXY location) {
    DominoForkRoad* nearest_fork_road = nullptr;
    uint64_t nearest_distance_squared_low = 0U;
    bool nearest_distance_squared_high = false;
    for (domino_nav_vertex_index_t vertex_index = 0U; vertex_index < vertex_count; vertex_index++) {
        DominoForkRoad* fork_road = fork_road_list[vertex_index];
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

/** @brief road_line 非空且缓存完整；可写实体上的顶点下标仍须检查。 */
static DOMINO_CODE navFillRoadLinePath(DominoRoadLine* road_line, domino_nav_vertex_index_t vertex_count, DominoForkRoad* const* fork_road_list,
                                       PathInfo (*path_matrix)[vertex_count], domino_nav_vertex_index_t start_vertex_index,
                                       domino_nav_vertex_index_t target_vertex_index) {
    if (start_vertex_index >= vertex_count || target_vertex_index >= vertex_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                          "cached fork vertex index is invalid: start=%u target=%u vertex_count=%u", (unsigned)start_vertex_index,
                          (unsigned)target_vertex_index, (unsigned)vertex_count);
        return ERR_INVALID_DATA;
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
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navigation road line is null");
        return ERR_NULL_POINTER;
    }
    if (!navStateIsReady()) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "cannot navigate before Nav initialization");
        return ERR_NOT_INITIALIZED;
    }

    const uint8_t region_index = road_line->region_index;
    const uint8_t road_network_type = road_line->type;
    const DominoNavDataSegment* segment = navStateFindSegment(&domino_all_fork_road_nav_data_segment_list, region_index, road_network_type);
    if (!segment || segment->total_count == 0U) {
        return ERR_NOT_FOUND;
    }

    const DominoNavPathCache* cache = navStateGetPathCache(region_index, road_network_type);
    if (!cache) {
        DOMINO_CODE result = navPlanSegmentPaths(segment, nullptr);
        if (result != CODE_OK) {
            return result;
        }
        /* 非空分段规划成功时已提交完整缓存，调用期间分段与缓存不会并发变化。 */
        cache = navStateGetPathCache(region_index, road_network_type);
    }

    const domino_nav_vertex_index_t vertex_count = cache->vertex_count;
    DominoForkRoad* const* fork_road_list = cache->fork_road_list;
    DominoForkRoad* start_fork_road = nullptr;
    DominoForkRoad* target_fork_road = nullptr;

    if (road_line->current_fork_road_id != 0U) {
        DOMINO_CODE result = navStateFindForkRoadById(road_line->current_fork_road_id, &start_fork_road);
        if (result != CODE_OK) {
            if (result == ERR_NOT_FOUND) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navigation start fork is missing: id=%u",
                                  road_line->current_fork_road_id);
            }
            return result;
        }
        if (start_fork_road->status != 0U) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navigation start fork is inactive: id=%u status=%u",
                              start_fork_road->id, (unsigned)start_fork_road->status);
            return ERR_GAME_STATE_INVALID;
        }
        if (start_fork_road->region_index != region_index || start_fork_road->type != road_network_type) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                              "navigation start fork is outside requested network: id=%u region=%u type=%u", start_fork_road->id,
                              (unsigned)region_index, (unsigned)road_network_type);
            return ERR_INVALID_PARAM;
        }
    } else if (!dominoLocationXYIsNull(road_line->current_location)) {
        start_fork_road = navFindNearestForkRoad(vertex_count, fork_road_list, road_line->current_location);
        road_line->current_fork_road_id = start_fork_road->id;
    } else {
        if (dominoLocationXYIsNull(road_line->start_location) || dominoLocationXYIsNull(road_line->target_location)) {
            DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                                  "navigation requires start and target locations when no current position is supplied");
            return ERR_INVALID_PARAM;
        }
        start_fork_road = navFindNearestForkRoad(vertex_count, fork_road_list, road_line->start_location);
        target_fork_road = navFindNearestForkRoad(vertex_count, fork_road_list, road_line->target_location);

        road_line->target_fork_road_id = target_fork_road->id;
        road_line->start_fork_road_id = start_fork_road->id;
        road_line->current_fork_road_id = start_fork_road->id;
        road_line->current_location = road_line->start_location;
        road_line->region_index = region_index;
        road_line->type = road_network_type;
        road_line->status = 0U;
    }

    if (!target_fork_road && road_line->target_fork_road_id != 0U) {
        DOMINO_CODE result = navStateFindForkRoadById(road_line->target_fork_road_id, &target_fork_road);
        if (result != CODE_OK && result != ERR_NOT_FOUND) {
            return result;
        }
    }
    if (!target_fork_road && !dominoLocationXYIsNull(road_line->target_location)) {
        target_fork_road = navFindNearestForkRoad(vertex_count, fork_road_list, road_line->target_location);
        road_line->target_fork_road_id = target_fork_road->id;
    }
    if (!target_fork_road) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navigation target cannot be resolved: id=%u",
                          road_line->target_fork_road_id);
        return road_line->target_fork_road_id == 0U ? ERR_INVALID_PARAM : ERR_NOT_FOUND;
    }
    if (target_fork_road->status != 0U) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "navigation target fork is inactive: id=%u status=%u",
                          target_fork_road->id, (unsigned)target_fork_road->status);
        return ERR_GAME_STATE_INVALID;
    }
    if (target_fork_road->region_index != region_index || target_fork_road->type != road_network_type) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                          "navigation target fork is outside requested network: id=%u region=%u type=%u", target_fork_road->id,
                          (unsigned)region_index, (unsigned)road_network_type);
        return ERR_INVALID_PARAM;
    }

    PathInfo(*path_matrix)[vertex_count] = (PathInfo(*)[vertex_count])cache->path_matrix;
    return navFillRoadLinePath(road_line, vertex_count, fork_road_list, path_matrix, start_fork_road->temp_index, target_fork_road->temp_index);
}

static DOMINO_CODE navPlanSegmentPaths(const DominoNavDataSegment* segment, FloydPathPlanningResult* out_stat) {
    const uint8_t region_index = segment->region_index;
    const uint8_t road_network_type = segment->road_network_type;
    const uint32_t vertex_count_u32 = segment->total_count;
    if (vertex_count_u32 == 0U) {
        navStateInvalidatePathCache(region_index, road_network_type);
        if (out_stat) {
            *out_stat = (FloydPathPlanningResult){0};
        }
        return CODE_OK;
    }
    if (vertex_count_u32 > MAX_FORK_ROAD_COUNT_PER_NETWORK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                          "path planning network exceeds vertex capacity: region=%u type=%u count=%u limit=%u", (unsigned)region_index,
                          (unsigned)road_network_type, vertex_count_u32, (unsigned)MAX_FORK_ROAD_COUNT_PER_NETWORK);
        return ERR_CAPACITY_EXCEEDED;
    }
    const domino_nav_vertex_index_t vertex_count = (domino_nav_vertex_index_t)vertex_count_u32;

    DominoNavPathCache* cache = navStatePreparePathCache(region_index, road_network_type);
    if (!cache) {
        return ERR_MEMORY_ALLOC;
    }

    DominoForkRoad** fork_road_list = (DominoForkRoad**)calloc((size_t)vertex_count, sizeof(*fork_road_list));
    if (!fork_road_list) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "failed to allocate path planning fork table: vertex_count=%u",
                          (unsigned)vertex_count);
        return ERR_MEMORY_ALLOC;
    }
    DOMINO_CODE result = navFillForkRoadListCache(segment, fork_road_list);
    if (result != CODE_OK) {
        free(fork_road_list);
        return result;
    }

    VertexEdges* vertices = (VertexEdges*)calloc((size_t)vertex_count, sizeof(*vertices));
    if (!vertices) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "failed to allocate path planning edges: vertex_count=%u",
                          (unsigned)vertex_count);
        free(fork_road_list);
        return ERR_MEMORY_ALLOC;
    }

    for (size_t vertex_index = 0U; vertex_index < (size_t)vertex_count; vertex_index++) {
        DominoForkRoad* from_fork_road = fork_road_list[vertex_index];
        VertexEdges* vertex_edges = &vertices[vertex_index];
        vertex_edges->road_fork_id = from_fork_road->id;
        if (from_fork_road->to_fork_road_count > MAX_EDGES_PER_VERTEX) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                              "path planning fork has invalid outgoing edge count: id=%u count=%u", from_fork_road->id,
                              (unsigned)from_fork_road->to_fork_road_count);
            result = ERR_INVALID_DATA;
            goto cleanup;
        }
        for (uint8_t edge_index = 0U; edge_index < from_fork_road->to_fork_road_count; edge_index++) {
            const domino_fork_road_id_t target_id = from_fork_road->to_fork_road_id[edge_index];
            if (target_id == 0U || target_id == from_fork_road->id) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                                  "path planning edge has null or identical endpoint: start=%u target=%u", from_fork_road->id, target_id);
                result = ERR_INVALID_DATA;
                goto cleanup;
            }
            DominoForkRoad* target_fork_road = nullptr;
            result = navStateFindForkRoadById(target_id, &target_fork_road);
            if (result != CODE_OK && result != ERR_NOT_FOUND) {
                goto cleanup;
            }
            if (!target_fork_road || target_fork_road->status != 0U || target_fork_road->region_index != region_index ||
                target_fork_road->type != road_network_type || target_fork_road->temp_index >= vertex_count ||
                fork_road_list[target_fork_road->temp_index] != target_fork_road) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                                  "path planning edge target is missing or inconsistent with network cache: start=%u target=%u", from_fork_road->id,
                                  target_id);
                result = ERR_INVALID_DATA;
                goto cleanup;
            }
            DominoRoad* road = nullptr;
            result = navStateFindRoadById(navBuildRoadId(from_fork_road->id, target_id), &road);
            if (result != CODE_OK && result != ERR_NOT_FOUND) {
                goto cleanup;
            }
            if (!road || road->status != 0U || road->region_index != region_index || road->type != road_network_type ||
                !dominoNavEdgeWeightIsPassable(road->weight[0])) {
                DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                                  "path planning edge has no matching active passable road: start=%u target=%u", from_fork_road->id, target_id);
                result = ERR_INVALID_DATA;
                goto cleanup;
            }
            vertex_edges->edges[vertex_edges->count].target_vertex_index = target_fork_road->temp_index;
            vertex_edges->edges[vertex_edges->count].weight = road->weight[0];
            vertex_edges->count++;
        }
    }

    const size_t matrix_element_count = (size_t)vertex_count * (size_t)vertex_count;
    PathInfo* path_matrix = (PathInfo*)calloc(matrix_element_count, sizeof(*path_matrix));
    if (!path_matrix) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "failed to allocate Floyd path matrix: elements=%zu element_size=%zu",
                          matrix_element_count, sizeof(*path_matrix));
        result = ERR_MEMORY_ALLOC;
        goto cleanup;
    }

    FloydPathPlanningResult stat = floydPathPlanning(vertex_count, vertices, (PathInfo(*)[vertex_count])path_matrix);
    free(vertices);

    navStateCommitPathCache(cache, path_matrix, fork_road_list, vertex_count);

    if (out_stat) {
        *out_stat = stat;
    }
    return CODE_OK;

cleanup:
    free(vertices);
    free(fork_road_list);
    return result;
}

DOMINO_CODE dominoNavPathPlanning(uint8_t region_index, uint8_t road_network_type, FloydPathPlanningResult* out_stat) {
    if (!navStateIsReady()) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "cannot plan paths before Nav initialization");
        return ERR_NOT_INITIALIZED;
    }
    const DominoNavDataSegment* segment = navStateFindSegment(&domino_all_fork_road_nav_data_segment_list, region_index, road_network_type);
    if (!segment) {
        return ERR_NOT_FOUND;
    }
    return navPlanSegmentPaths(segment, out_stat);
}
