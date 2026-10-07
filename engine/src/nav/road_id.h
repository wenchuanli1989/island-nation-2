#ifndef DOMINO_ENGINE_NAV_ROAD_ID_H
#define DOMINO_ENGINE_NAV_ROAD_ID_H

#include <stdint.h>

#include "domino_shared_types.h"

/** @brief 高 32 位保存起点 ID，低 32 位保存终点 ID。 */
static inline domino_road_id_t navBuildRoadId(domino_fork_road_id_t start_fork_road_id, domino_fork_road_id_t target_fork_road_id) {
    return ((domino_road_id_t)start_fork_road_id << 32U) | (domino_road_id_t)target_fork_road_id;
}

static inline domino_fork_road_id_t navExtractRoadStartForkRoadId(domino_road_id_t road_id) {
    return (domino_fork_road_id_t)(road_id >> 32U);
}

static inline domino_fork_road_id_t navExtractRoadTargetForkRoadId(domino_road_id_t road_id) {
    return (domino_fork_road_id_t)(road_id & UINT32_MAX);
}

#endif
