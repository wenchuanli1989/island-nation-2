#include "graph.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "domino_shared_common.h"
#include "road_id.h"
#include "state.h"
#include "storage_view.h"

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
    for (size_t index = 0U; index < kv_size(domino_all_fork_road_list); index++) {
        DominoForkRoad* fork_road = &kv_A(domino_all_fork_road_list, index);
        memset(fork_road->to_fork_road_id, 0, sizeof(fork_road->to_fork_road_id));
        fork_road->to_fork_road_count = 0U;
        fork_road->temp_index = 0U;
    }

    /* 当前格式的正确存档已保证道路端点、路网和唯一性；这里只恢复未持久化的邻接。 */
    for (size_t index = 0U; index < kv_size(domino_all_road_list); index++) {
        const DominoRoad* road = &kv_A(domino_all_road_list, index);
        if (road->status != 0U || !dominoNavEdgeWeightIsPassable(road->weight[0])) {
            continue;
        }
        const domino_fork_road_id_t start_id = navExtractRoadStartForkRoadId(road->id);
        const domino_fork_road_id_t target_id = navExtractRoadTargetForkRoadId(road->id);
        DominoForkRoad* start = nullptr;
        (void)navStateFindForkRoadById(start_id, &start);
        assert(start != nullptr);
        if (start->to_fork_road_count >= MAX_EDGES_PER_VERTEX) {
            DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "loaded fork road exceeds outgoing edge capacity: id=%u capacity=%u", start_id,
                                   (unsigned)MAX_EDGES_PER_VERTEX);
        }
        navGraphAppendEdge(start, target_id);
    }
    navStateInvalidateAllPathCaches();
}
