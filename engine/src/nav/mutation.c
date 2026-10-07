#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../host/entry.h"
#include "domino_shared_nav.h"
#include "graph.h"
#include "klib/kvec.h"
#include "road_id.h"
#include "state.h"
#include "storage_view.h"

/**
 * @brief 在原槽位提交道路恢复。
 * @note 调用方已确认道路已删除、端点活跃且分组匹配，邻接尚不存在且可追加；road_segment_index 为本次定位结果。
 */
static void navRestoreRoad(DominoRoad* road, DominoForkRoad* start_fork_road, domino_fork_road_id_t target_id, domino_nav_weight_atom_t weight,
                           int road_segment_index) {
    const uint8_t region_index = start_fork_road->region_index;
    const uint8_t network_type = start_fork_road->type;
    const uint32_t road_index = (uint32_t)(road - domino_all_road_list.a);
    navGraphAppendEdge(start_fork_road, target_id);
    road->weight[0] = weight;
    road->status = 0U;
    if (road_segment_index >= 0) {
        DominoNavDataSegment* segment = &kv_A(domino_all_road_nav_data_segment_list, (size_t)road_segment_index);
        segment->total_count++;
        const uint64_t init_end = (uint64_t)segment->init_index + segment->init_count;
        /* 原基段中的实体仍由基段扫描；其余恢复记录须被增量范围覆盖。 */
        if (road_index >= init_end && road_index < segment->increment_index) {
            segment->increment_index = road_index;
        }
    } else {
        kv_push(DominoNavDataSegment, domino_all_road_nav_data_segment_list,
                ((DominoNavDataSegment){
                    .increment_index = road_index,
                    .total_count = 1U,
                    .region_index = region_index,
                    .road_network_type = network_type,
                }));
    }
    navStateInvalidatePathCache(region_index, network_type);
    navStateMarkDirty();
}

DOMINO_CODE dominoAddRoad(DominoForkRoad* start_fork_road, DominoForkRoad* target_fork_road, domino_nav_weight_atom_t weight, uint8_t region_index,
                          uint8_t network_type) {
    if (!start_fork_road || !target_fork_road || start_fork_road == target_fork_road || !dominoNavEdgeWeightIsPassable(weight)) {
        return ERR_INVALID_PARAM;
    }
    if (!navStateIsReady()) {
        return ERR_NOT_INITIALIZED;
    }

    domino_fork_road_id_t start_id = start_fork_road->id;
    domino_fork_road_id_t target_id = target_fork_road->id;
    const bool create_start = start_id == 0U;
    const bool create_target = target_id == 0U;
    DominoForkRoad* existing_start = nullptr;
    DominoForkRoad start_value;
    DominoForkRoad target_value;
    if (create_start) {
        start_value = *start_fork_road;
        start_value.region_index = region_index;
        start_value.type = network_type;
        start_value.status = 0U;
        memset(start_value.to_fork_road_id, 0, sizeof(start_value.to_fork_road_id));
        start_value.to_fork_road_count = 0U;
        start_value.temp_index = 0U;
    }
    if (create_target) {
        target_value = *target_fork_road;
        target_value.region_index = region_index;
        target_value.type = network_type;
        target_value.status = 0U;
        memset(target_value.to_fork_road_id, 0, sizeof(target_value.to_fork_road_id));
        target_value.to_fork_road_count = 0U;
        target_value.temp_index = 0U;
    }

    if (!create_start) {
        existing_start = dominoGetForkRoadByID(start_id);
        if (!existing_start || existing_start->status != 0U || existing_start->region_index != region_index || existing_start->type != network_type) {
            return ERR_INVALID_DATA;
        }
    }
    if (!create_target) {
        const DominoForkRoad* existing = dominoGetForkRoadByID(target_id);
        if (!existing || existing->status != 0U || existing->region_index != region_index || existing->type != network_type) {
            return ERR_INVALID_DATA;
        }
    }

    domino_road_id_t road_id = 0U;
    DominoRoad* existing_road = nullptr;
    if (!create_start && !create_target) {
        if (start_id == target_id) {
            return ERR_INVALID_PARAM;
        }
        road_id = navBuildRoadId(start_id, target_id);
        existing_road = dominoGetRoadByID(road_id);
        if (existing_road && existing_road->status == 0U) {
            return ERR_ALREADY_EXISTS;
        }
    }
    if (existing_start) {
        if (existing_start->to_fork_road_count >= MAX_EDGES_PER_VERTEX) {
            return ERR_OUT_OF_RANGE;
        }
        if (!create_target && navGraphHasEdge(existing_start, target_id)) {
            return ERR_INVALID_DATA;
        }
    }
    const int road_segment_index = navStateFindSegmentIndex(&domino_all_road_nav_data_segment_list, region_index, network_type);
    if (existing_road) {
        if (existing_road->region_index != region_index || existing_road->type != network_type) {
            return ERR_INVALID_DATA;
        }
        navRestoreRoad(existing_road, existing_start, target_id, weight, road_segment_index);
        return CODE_OK;
    }

    const size_t old_fork_count = kv_size(domino_all_fork_road_list);
    const size_t old_road_count = kv_size(domino_all_road_list);
    /* 参数可能指向全局数组；串行追加只改变基址，不改变已有节点的下标。 */
    const size_t start_index = create_start ? old_fork_count : (size_t)(existing_start - domino_all_fork_road_list.a);
    const uint32_t fork_segment_delta = (create_start ? 1U : 0U) + (create_target ? 1U : 0U);
    if (old_fork_count > DOMINO_NAV_ID_MAP_ENTITY_COUNT_MAX - fork_segment_delta || old_road_count >= DOMINO_NAV_ID_MAP_ENTITY_COUNT_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    const size_t required_fork_count = old_fork_count + (size_t)fork_segment_delta;
    const int fork_segment_index = navStateFindSegmentIndex(&domino_all_fork_road_nav_data_segment_list, region_index, network_type);
    if (fork_segment_index < 0 && fork_segment_delta == 0U) {
        return ERR_INVALID_DATA;
    }
    const uint32_t active_fork_count =
        fork_segment_index >= 0 ? kv_A(domino_all_fork_road_nav_data_segment_list, (size_t)fork_segment_index).total_count : 0U;
    if ((uint64_t)active_fork_count + fork_segment_delta > MAX_FORK_ROAD_COUNT_PER_NETWORK) {
        return ERR_OUT_OF_RANGE;
    }
    if (fork_segment_delta > 0U) {
        /* Nav 修改须串行执行，在原子递增前检查本次新增路口所需的 ID 范围。 */
        const domino_fork_road_id_t next_id = atomic_load_explicit(&g_domino_global_increment_id.fork_road_increment_id, memory_order_relaxed);
        if (next_id > UINT32_MAX - fork_segment_delta) {
            return ERR_OUT_OF_RANGE;
        }
        if (create_start) {
            DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.fork_road_increment_id, start_id);
            start_value.id = start_id;
        }
        if (create_target) {
            DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.fork_road_increment_id, target_id);
            target_value.id = target_id;
        }
        road_id = navBuildRoadId(start_id, target_id);
    }
    /* 邻接字段可被外部改写；新 ID 分配后也须确认当前起点没有这条出边。 */
    if (existing_start && create_target && navGraphHasEdge(existing_start, target_id)) {
        return ERR_INVALID_DATA;
    }

    DOMINO_CODE result;
    khint_t target_map_slot = 0U;
    int absent = 0;
    if (create_start) {
        khint_t slot = dominoForkRoadIdMap_put(dominoForkRoadIdMap, start_id, &absent);
        if (absent != 1) {
            return absent < 0 ? ERR_MEMORY_ALLOC : ERR_ALREADY_EXISTS;
        }
        kh_val(dominoForkRoadIdMap, slot) = (uint32_t)old_fork_count;
    }
    if (create_target) {
        target_map_slot = dominoForkRoadIdMap_put(dominoForkRoadIdMap, target_id, &absent);
        if (absent != 1) {
            result = absent < 0 ? ERR_MEMORY_ALLOC : ERR_ALREADY_EXISTS;
            goto rollback_start;
        }
        kh_val(dominoForkRoadIdMap, target_map_slot) = (uint32_t)(old_fork_count + (create_start ? 1U : 0U));
    }
    {
        khint_t slot = dominoRoadIdMap_put(dominoRoadIdMap, road_id, &absent);
        if (absent != 1) {
            result = absent < 0 ? ERR_MEMORY_ALLOC : ERR_ALREADY_EXISTS;
            goto rollback_target;
        }
        kh_val(dominoRoadIdMap, slot) = (uint32_t)old_road_count;
    }

    /* 所有可返回失败的步骤已经完成；kvec 分配失败按容器契约终止进程，不产生可回滚的错误返回。 */
    if (required_fork_count > kv_max(domino_all_fork_road_list)) {
        navStateInvalidateAllPathCaches();
    }
    if (create_start) {
        kv_push(DominoForkRoad, domino_all_fork_road_list, start_value);
    }
    if (create_target) {
        kv_push(DominoForkRoad, domino_all_fork_road_list, target_value);
    }
    kv_push(DominoRoad, domino_all_road_list,
            ((DominoRoad){
                .id = road_id,
                .weight = {weight},
                .type = network_type,
                .status = 0U,
                .region_index = region_index,
            }));
    /* 追加只可能搬迁数组基址；按保存的下标提交邻接和分组计数。 */
    navGraphAppendEdge(&kv_A(domino_all_fork_road_list, start_index), target_id);
    if (fork_segment_index >= 0) {
        kv_A(domino_all_fork_road_nav_data_segment_list, (size_t)fork_segment_index).total_count += fork_segment_delta;
    } else {
        kv_push(DominoNavDataSegment, domino_all_fork_road_nav_data_segment_list,
                ((DominoNavDataSegment){
                    .increment_index = (uint32_t)old_fork_count,
                    .total_count = fork_segment_delta,
                    .region_index = region_index,
                    .road_network_type = network_type,
                }));
    }
    if (road_segment_index >= 0) {
        kv_A(domino_all_road_nav_data_segment_list, (size_t)road_segment_index).total_count++;
    } else {
        kv_push(DominoNavDataSegment, domino_all_road_nav_data_segment_list,
                ((DominoNavDataSegment){
                    .increment_index = (uint32_t)old_road_count,
                    .total_count = 1U,
                    .region_index = region_index,
                    .road_network_type = network_type,
                }));
    }
    navStateInvalidatePathCache(region_index, network_type);
    navStateMarkDirty();

    if (create_start) {
        start_fork_road->id = start_id;
        start_fork_road->region_index = region_index;
        start_fork_road->type = network_type;
    }
    if (create_target) {
        target_fork_road->id = target_id;
        target_fork_road->region_index = region_index;
        target_fork_road->type = network_type;
    }
    return CODE_OK;

rollback_target:
    /* target 插入后没有再修改 fork map，此槽位仍有效。 */
    if (create_target) {
        dominoForkRoadIdMap_del(dominoForkRoadIdMap, target_map_slot);
    }
rollback_start:
    /* target 插入扩容或删除可能改变 start 槽位，按稳定 ID 重新定位。 */
    if (create_start) {
        khint_t slot = dominoForkRoadIdMap_get(dominoForkRoadIdMap, start_id);
        dominoForkRoadIdMap_del(dominoForkRoadIdMap, slot);
    }
    return result;
}

DOMINO_CODE dominoRemoveRoad(domino_road_id_t road_id) {
    DominoRoad* road = dominoGetRoadByID(road_id);
    if (!road) {
        return ERR_NOT_FOUND;
    }
    if (road->status != 0U) {
        return ERR_GAME_STATE_INVALID;
    }

    domino_fork_road_id_t start_id = navExtractRoadStartForkRoadId(road_id);
    domino_fork_road_id_t target_id = navExtractRoadTargetForkRoadId(road_id);
    DominoForkRoad* start = dominoGetForkRoadByID(start_id);
    DominoForkRoad* target = dominoGetForkRoadByID(target_id);
    if (!start || !target || start->status != 0U || target->status != 0U || start->region_index != road->region_index ||
        target->region_index != road->region_index || start->type != road->type || target->type != road->type) {
        return ERR_INVALID_DATA;
    }
    if (start->to_fork_road_count > MAX_EDGES_PER_VERTEX) {
        return ERR_INVALID_DATA;
    }

    bool remove_start = true;
    bool remove_target = true;
    /* 一次扫描同时判断两端是否孤立；不可通行的活跃道路也保留关联路口。 */
    for (size_t index = 0U; index < kv_size(domino_all_road_list); index++) {
        const DominoRoad* other_road = &kv_A(domino_all_road_list, index);
        if (other_road->status != 0U || other_road->id == road_id) {
            continue;
        }
        const domino_fork_road_id_t other_start_id = navExtractRoadStartForkRoadId(other_road->id);
        const domino_fork_road_id_t other_target_id = navExtractRoadTargetForkRoadId(other_road->id);
        if (other_start_id == start_id || other_target_id == start_id) {
            remove_start = false;
        }
        if (other_start_id == target_id || other_target_id == target_id) {
            remove_target = false;
        }
        if (!remove_start && !remove_target) {
            break;
        }
    }
    const uint32_t fork_segment_delta = (remove_start ? 1U : 0U) + (remove_target ? 1U : 0U);

    const int fork_segment_index = navStateFindSegmentIndex(&domino_all_fork_road_nav_data_segment_list, road->region_index, road->type);
    const int road_segment_index = navStateFindSegmentIndex(&domino_all_road_nav_data_segment_list, road->region_index, road->type);
    if (fork_segment_index < 0 || road_segment_index < 0) {
        return ERR_INVALID_DATA;
    }
    DominoNavDataSegment* fork_segment = &kv_A(domino_all_fork_road_nav_data_segment_list, (size_t)fork_segment_index);
    DominoNavDataSegment* road_segment = &kv_A(domino_all_road_nav_data_segment_list, (size_t)road_segment_index);
    if (fork_segment->total_count < fork_segment_delta || road_segment->total_count == 0U) {
        return ERR_INVALID_DATA;
    }

    /* 邻接校验只扫描一次；所有可能返回失败的检查都在状态和计数提交之前。 */
    if (dominoNavEdgeWeightIsPassable(road->weight[0])) {
        if (!navGraphRemoveEdge(start, target_id)) {
            return ERR_INVALID_DATA;
        }
    } else if (navGraphHasEdge(start, target_id)) {
        return ERR_INVALID_DATA;
    }
    road->status = 1U;
    if (remove_start) {
        start->status = 1U;
    }
    if (remove_target) {
        target->status = 1U;
    }
    fork_segment->total_count -= fork_segment_delta;
    road_segment->total_count--;
    navStateInvalidatePathCache(road->region_index, road->type);
    navStateMarkDirty();
    return CODE_OK;
}
