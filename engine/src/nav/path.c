#include "path.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "domino_shared_common.h"
#include "floyd.h"
#include "klib/kvec.h"
#include "state.h"

static void buildSegmentPathCache(const DominoNavSegment* segment, DominoNavFloydStats* out_stat);

/** @brief 按分段收集组内活跃路口作为顶点（基础段在前、增量尾在后），并回写各路口的 vertex_index；数量不符致命退出。 */
static void fillSegmentVertices(const DominoNavSegment* segment, DominoForkRoad** fork_road_list_out) {
    const domino_nav_vertex_index_t vertex_count = (domino_nav_vertex_index_t)segment->total_count;
    const size_t fork_road_count = kv_size(g_domino_fork_road_list);
    // 64 位加法避免基础段终点溢出。
    const uint64_t init_end_u64 = (uint64_t)segment->base_index + (uint64_t)segment->base_count;
    if (init_end_u64 > fork_road_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "navFillForkRoadListCache init end out of range");
    }
    if (segment->tail_index > (uint32_t)fork_road_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "navFillForkRoadListCache tail_index out of range");
    }

    domino_nav_vertex_index_t filled_count = 0U;
    const uint8_t region_index = segment->region_index;
    const uint8_t road_network_type = segment->road_network_type;
    const uint32_t init_end = (uint32_t)init_end_u64;

    for (uint32_t index = segment->base_index; index < init_end; index++) {
        DominoForkRoad* fork_road = &kv_A(g_domino_fork_road_list, (size_t)index);
        if (fork_road->status != 0U) {
            continue;
        }
        if (fork_road->region_index != region_index || fork_road->type != road_network_type) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "fork road base entity at index=%u does not match its segment key", index);
        }
        if (filled_count >= vertex_count) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "fork road segment vertex overflow (capacity=%u)", (uint32_t)vertex_count);
        }
        fork_road_list_out[filled_count] = fork_road;
        fork_road->vertex_index = filled_count;
        filled_count++;
    }

    for (uint32_t index = segment->tail_index; index < (uint32_t)fork_road_count; index++) {
        DominoForkRoad* fork_road = &kv_A(g_domino_fork_road_list, (size_t)index);
        if (fork_road->status != 0U || fork_road->region_index != region_index || fork_road->type != road_network_type) {
            continue;
        }
        if (filled_count >= vertex_count) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "fork road segment vertex overflow (capacity=%u)", (uint32_t)vertex_count);
        }
        fork_road_list_out[filled_count] = fork_road;
        fork_road->vertex_index = filled_count;
        filled_count++;
    }

    if (filled_count != vertex_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "fork road segment vertex count mismatch: expected=%u actual=%u", (uint32_t)vertex_count,
                               (uint32_t)filled_count);
    }
}

/** @brief 返回离坐标最近的顶点路口；缓存保证顶点数非零。 */
static DominoForkRoad* findNearestForkRoad(domino_nav_vertex_index_t vertex_count, DominoForkRoad* const* fork_road_list, DominoLocationXY location) {
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
        // 两个 32 位坐标差的平方和最多 65 位，用 high 保存加法进位。
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

/** @brief 写入起点到终点的中间路口和路径权重；顶点下标来自可写实体，仍须校验。 */
static DOMINO_CODE fillRoadLinePath(DominoRoadLine* road_line, domino_nav_vertex_index_t vertex_count, DominoForkRoad* const* fork_road_list,
                                    DominoNavPathCell (*path_matrix)[vertex_count], domino_nav_vertex_index_t start_vertex_index,
                                    domino_nav_vertex_index_t target_vertex_index) {
    if (start_vertex_index >= vertex_count || target_vertex_index >= vertex_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "cached fork vertex index is invalid: start=%u target=%u vertex_count=%u",
                               (unsigned)start_vertex_index, (unsigned)target_vertex_index, (unsigned)vertex_count);
    }

    for (uint32_t index = 0U; index < DOMINO_MAX_FORK_ROAD_COUNT_PER_ROAD_LINE; index++) {
        road_line->fork_road_id_list[index] = 0U;
    }
    road_line->fork_road_count = 0U;

    if (start_vertex_index == target_vertex_index) {
        road_line->remaining_distance = 0U;
        return CODE_OK;
    }

    DominoNavPathCell step_list[DOMINO_MAX_FORK_ROAD_COUNT_PER_ROAD_LINE];
    int step_count = 0;
    DOMINO_CODE result = navFloydTracePath(step_list, DOMINO_MAX_FORK_ROAD_COUNT_PER_ROAD_LINE, &step_count, vertex_count, path_matrix,
                                           start_vertex_index, target_vertex_index);
    if (result != CODE_OK) {
        return result;
    }

    road_line->remaining_distance = path_matrix[start_vertex_index][target_vertex_index].min_weight;

    // 回溯结果按终点到起点排列，翻转为行进顺序。
    for (int index = 0; index < step_count; index++) {
        domino_nav_vertex_index_t previous_vertex_index = step_list[step_count - 1 - index].pre_vertex_index;
        road_line->fork_road_id_list[index] = fork_road_list[previous_vertex_index]->id;
    }
    road_line->fork_road_count = (uint16_t)step_count;
    return CODE_OK;
}

DOMINO_CODE dominoNavRouteRoadLine(DominoRoadLine* road_line) {
    if (!road_line) {
        return ERR_NULL_POINTER;
    }
    const uint8_t region_index = road_line->region_index;
    const uint8_t road_network_type = road_line->type;
    const DominoNavSegment* segment = navStateFindSegment(&g_domino_fork_road_segment_list, region_index, road_network_type);
    if (!segment || segment->total_count == 0U) {
        return ERR_NOT_FOUND;
    }

    const DominoNavPathCache* cache = navStateGetPathCache(region_index, road_network_type);
    if (!cache) {
        buildSegmentPathCache(segment, nullptr);
        // 非空分段构建后缓存必然有效。
        cache = navStateGetPathCache(region_index, road_network_type);
    }

    const domino_nav_vertex_index_t vertex_count = cache->vertex_count;
    DominoForkRoad* const* fork_road_list = cache->fork_road_list;
    DominoForkRoad* start_fork_road = nullptr;
    DominoForkRoad* target_fork_road = nullptr;

    if (road_line->current_fork_road_id != 0U) {
        DOMINO_CODE result = navStateFindForkRoadById(road_line->current_fork_road_id, &start_fork_road);
        if (result != CODE_OK) {
            return result;
        }
        if (start_fork_road->status != 0U) {
            return ERR_GAME_STATE_INVALID;
        }
        if (start_fork_road->region_index != region_index || start_fork_road->type != road_network_type) {
            return ERR_INVALID_PARAM;
        }
    } else if (!dominoLocationXYIsNull(road_line->current_location)) {
        start_fork_road = findNearestForkRoad(vertex_count, fork_road_list, road_line->current_location);
        road_line->current_fork_road_id = start_fork_road->id;
    } else {
        if (dominoLocationXYIsNull(road_line->start_location) || dominoLocationXYIsNull(road_line->target_location)) {
            return ERR_INVALID_PARAM;
        }
        start_fork_road = findNearestForkRoad(vertex_count, fork_road_list, road_line->start_location);
        target_fork_road = findNearestForkRoad(vertex_count, fork_road_list, road_line->target_location);

        road_line->target_fork_road_id = target_fork_road->id;
        road_line->start_fork_road_id = start_fork_road->id;
        road_line->current_fork_road_id = start_fork_road->id;
        road_line->current_location = road_line->start_location;
        road_line->region_index = region_index;
        road_line->type = road_network_type;
        road_line->status = 0U;
    }

    if (!target_fork_road && road_line->target_fork_road_id != 0U) {
        (void)navStateFindForkRoadById(road_line->target_fork_road_id, &target_fork_road);
    }
    if (!target_fork_road && !dominoLocationXYIsNull(road_line->target_location)) {
        target_fork_road = findNearestForkRoad(vertex_count, fork_road_list, road_line->target_location);
        road_line->target_fork_road_id = target_fork_road->id;
    }
    if (!target_fork_road) {
        return road_line->target_fork_road_id == 0U ? ERR_INVALID_PARAM : ERR_NOT_FOUND;
    }
    if (target_fork_road->status != 0U) {
        return ERR_GAME_STATE_INVALID;
    }
    if (target_fork_road->region_index != region_index || target_fork_road->type != road_network_type) {
        return ERR_INVALID_PARAM;
    }

    DominoNavPathCell(*path_matrix)[vertex_count] = (DominoNavPathCell(*)[vertex_count])cache->path_matrix;
    return fillRoadLinePath(road_line, vertex_count, fork_road_list, path_matrix, start_fork_road->vertex_index, target_fork_road->vertex_index);
}

/** @brief 收集顶点、按活跃可通行道路建立出边并运行 Floyd，提交为分组的新缓存；图数据不一致致命退出。 */
static void buildSegmentPathCache(const DominoNavSegment* segment, DominoNavFloydStats* out_stat) {
    const uint8_t region_index = segment->region_index;
    const uint8_t road_network_type = segment->road_network_type;
    const uint32_t vertex_count_u32 = segment->total_count;
    if (vertex_count_u32 == 0U) {
        navStateInvalidatePathCache(region_index, road_network_type);
        if (out_stat) {
            *out_stat = (DominoNavFloydStats){0};
        }
        return;
    }
    if (vertex_count_u32 > DOMINO_MAX_FORK_ROAD_COUNT_PER_NETWORK) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "path planning network exceeds vertex capacity: region=%u type=%u count=%u limit=%u",
                               (unsigned)region_index, (unsigned)road_network_type, vertex_count_u32,
                               (unsigned)DOMINO_MAX_FORK_ROAD_COUNT_PER_NETWORK);
    }
    const domino_nav_vertex_index_t vertex_count = (domino_nav_vertex_index_t)vertex_count_u32;

    DominoNavPathCache* cache = navStatePreparePathCache(region_index, road_network_type);

    DominoForkRoad** fork_road_list = (DominoForkRoad**)calloc((size_t)vertex_count, sizeof(*fork_road_list));
    if (!fork_road_list) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate path planning fork table: vertex_count=%u", (unsigned)vertex_count);
    }
    fillSegmentVertices(segment, fork_road_list);

    DominoNavVertexEdges* vertex_list = (DominoNavVertexEdges*)calloc((size_t)vertex_count, sizeof(*vertex_list));
    if (!vertex_list) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate path planning edges: vertex_count=%u", (unsigned)vertex_count);
    }

    for (size_t vertex_index = 0U; vertex_index < (size_t)vertex_count; vertex_index++) {
        DominoForkRoad* source_fork_road = fork_road_list[vertex_index];
        DominoNavVertexEdges* vertex_ptr = &vertex_list[vertex_index];
        vertex_ptr->fork_road_id = source_fork_road->id;
        if (source_fork_road->to_fork_road_count > DOMINO_MAX_EDGE_COUNT_PER_VERTEX) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "path planning fork has invalid outgoing edge count: id=%u count=%u", source_fork_road->id,
                                   (unsigned)source_fork_road->to_fork_road_count);
        }
        for (uint8_t edge_index = 0U; edge_index < source_fork_road->to_fork_road_count; edge_index++) {
            const domino_fork_road_id_t target_id = source_fork_road->to_fork_road_id_list[edge_index];
            if (target_id == 0U || target_id == source_fork_road->id) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "path planning edge has null or identical endpoint: start=%u target=%u",
                                       source_fork_road->id, target_id);
            }
            DominoForkRoad* target_fork_road = nullptr;
            (void)navStateFindForkRoadById(target_id, &target_fork_road);
            if (!target_fork_road || target_fork_road->status != 0U || target_fork_road->region_index != region_index ||
                target_fork_road->type != road_network_type || target_fork_road->vertex_index >= vertex_count ||
                fork_road_list[target_fork_road->vertex_index] != target_fork_road) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA,
                                       "path planning edge target is missing or inconsistent with network cache: start=%u target=%u",
                                       source_fork_road->id, target_id);
            }
            DominoRoad* road = nullptr;
            (void)navStateFindRoadById(navBuildRoadId(source_fork_road->id, target_id), &road);
            if (!road || road->status != 0U || road->region_index != region_index || road->type != road_network_type ||
                !dominoNavEdgeWeightIsPassable(road->weight[0])) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "path planning edge has no matching active passable road: start=%u target=%u",
                                       source_fork_road->id, target_id);
            }
            vertex_ptr->edge_list[vertex_ptr->edge_count].target_vertex_index = target_fork_road->vertex_index;
            vertex_ptr->edge_list[vertex_ptr->edge_count].weight = road->weight[0];
            vertex_ptr->edge_count++;
        }
    }

    if ((size_t)vertex_count > SIZE_MAX / (size_t)vertex_count / sizeof(DominoNavPathCell)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "Floyd path matrix allocation size overflows: vertex_count=%u", (unsigned)vertex_count);
    }
    const size_t matrix_element_count = (size_t)vertex_count * (size_t)vertex_count;
    DominoNavPathCell* path_matrix = (DominoNavPathCell*)calloc(matrix_element_count, sizeof(*path_matrix));
    if (!path_matrix) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate Floyd path matrix: elements=%zu element_size=%zu", matrix_element_count,
                               sizeof(*path_matrix));
    }

    DominoNavFloydStats stats = navFloydBuildPaths(vertex_count, vertex_list, (DominoNavPathCell(*)[vertex_count])path_matrix);
    free(vertex_list);

    navStateCommitPathCache(cache, path_matrix, fork_road_list, vertex_count);

    if (out_stat) {
        *out_stat = stats;
    }
}

DOMINO_CODE dominoNavBuildPathCache(uint8_t region_index, uint8_t road_network_type, DominoNavFloydStats* out_stat) {
    const DominoNavSegment* segment = navStateFindSegment(&g_domino_fork_road_segment_list, region_index, road_network_type);
    if (!segment) {
        return ERR_NOT_FOUND;
    }
    buildSegmentPathCache(segment, out_stat);
    return CODE_OK;
}
