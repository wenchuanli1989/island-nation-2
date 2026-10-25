#include "graph.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "domino_shared_common.h"
#include "road_id.h"
#include "state.h"
#include "storage_view.h"

/** @brief 邻接重建的临时结果，预检全部通过后才写回实体。 */
typedef struct {
    domino_fork_road_id_t target_ids[MAX_EDGES_PER_VERTEX];
    uint8_t count;
} NavOutgoingEdges;

bool navGraphHasEdge(const DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id) {
    for (uint8_t edge_index = 0U; edge_index < fork_road->to_fork_road_count; edge_index++) {
        if (fork_road->to_fork_road_id[edge_index] == to_fork_road_id) {
            return true;
        }
    }
    return false;
}

void navGraphAppendEdge(DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id) {
    fork_road->to_fork_road_id[fork_road->to_fork_road_count] = to_fork_road_id;
    fork_road->to_fork_road_count++;
}

bool navGraphRemoveEdge(DominoForkRoad* fork_road, domino_fork_road_id_t to_fork_road_id) {
    for (uint8_t edge_index = 0U; edge_index < fork_road->to_fork_road_count; edge_index++) {
        if (fork_road->to_fork_road_id[edge_index] != to_fork_road_id) {
            continue;
        }
        const size_t move_count = (size_t)(fork_road->to_fork_road_count - edge_index - 1U);
        if (move_count > 0U) {
            memmove(&fork_road->to_fork_road_id[edge_index], &fork_road->to_fork_road_id[edge_index + 1U],
                    move_count * sizeof(domino_fork_road_id_t));
        }
        fork_road->to_fork_road_count--;
        fork_road->to_fork_road_id[fork_road->to_fork_road_count] = 0U;
        return true;
    }
    return false;
}

void navGraphRebuildAdjacency(void) {
    const size_t fork_count = kv_size(domino_all_fork_road_list);
    NavOutgoingEdges* outgoing_edges = nullptr;
    if (fork_count > SIZE_MAX / sizeof(*outgoing_edges)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "adjacency rebuild allocation size overflows: fork_count=%zu element_size=%zu", fork_count,
                               sizeof(*outgoing_edges));
    }
    if (fork_count > 0U) {
        outgoing_edges = (NavOutgoingEdges*)calloc(fork_count, sizeof(*outgoing_edges));
        if (!outgoing_edges) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate adjacency rebuild buffer: fork_count=%zu", fork_count);
        }
    }

    for (size_t index = 0U; index < kv_size(domino_all_road_list); index++) {
        const DominoRoad* road = &kv_A(domino_all_road_list, index);
        const domino_fork_road_id_t start_id = navExtractRoadStartForkRoadId(road->id);
        const domino_fork_road_id_t target_id = navExtractRoadTargetForkRoadId(road->id);
        DominoForkRoad* start = nullptr;
        (void)navStateFindForkRoadById(start_id, &start);
        DominoForkRoad* target = nullptr;
        (void)navStateFindForkRoadById(target_id, &target);
        if (!start || !target || start_id == target_id) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "road has missing or identical endpoints during adjacency rebuild: index=%zu start=%u target=%u",
                                   index, start_id, target_id);
        }
        if (road->status != 0U) {
            continue;
        }
        if (start->status != 0U || target->status != 0U || start->region_index != road->region_index || target->region_index != road->region_index ||
            start->type != road->type || target->type != road->type) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA,
                                   "road endpoints do not match active network during adjacency rebuild: index=%zu start=%u target=%u", index,
                                   start_id, target_id);
        }
        if (!dominoNavEdgeWeightIsPassable(road->weight[0])) {
            continue;
        }

        /* 查询结果已保证位于当前数组内，调用期间数组、map 与实体属性保持稳定。 */
        NavOutgoingEdges* edges = &outgoing_edges[(size_t)(start - domino_all_fork_road_list.a)];
        if (edges->count >= MAX_EDGES_PER_VERTEX) {
            DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "loaded fork road exceeds outgoing edge capacity: id=%u capacity=%u", start_id,
                                   (unsigned)MAX_EDGES_PER_VERTEX);
        }
        /* Storage 的唯一 road ID 同时保证这条有向边未重复。 */
        edges->target_ids[edges->count++] = target_id;
    }

    for (size_t index = 0U; index < fork_count; index++) {
        DominoForkRoad* fork_road = &kv_A(domino_all_fork_road_list, index);
        const NavOutgoingEdges* edges = &outgoing_edges[index];
        memcpy(fork_road->to_fork_road_id, edges->target_ids, sizeof(fork_road->to_fork_road_id));
        fork_road->to_fork_road_count = edges->count;
        fork_road->temp_index = 0U;
    }

    navStateInvalidateAllPathCaches();

    free(outgoing_edges);
}
