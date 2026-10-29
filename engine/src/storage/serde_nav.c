#include <stdlib.h>
#include <string.h>

#include "../nav/storage_view.h"
#include "domino_shared_common.h"
#include "io.h"
#include "klib/kvec.h"
#include "serde.h"
#include "serde_registry.h"

#define DOMINO_NAV_DATA_SEGMENT_KEY_COUNT ((size_t)UINT16_MAX + 1U)

/** @brief 仅保存分段布局；调用方提供已创建的 doc 和有效分段。 */
static yyjson_mut_val* serializeNavDataSegment(yyjson_mut_doc* doc, const DominoNavDataSegment* segment) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "init_index", segment->init_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "init_count", segment->init_count) ||
        !yyjson_mut_obj_add_uint(doc, object, "increment_index", segment->increment_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "total_count", segment->total_count) ||
        !yyjson_mut_obj_add_uint(doc, object, "region_index", segment->region_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "road_network_type", segment->road_network_type)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    return object;
}

static void deserializeNavDataSegment(yyjson_val* object, DominoNavDataSegment* out_segment) {
    *out_segment = (DominoNavDataSegment){
        .init_index = (uint32_t)yyjson_get_uint(yyjson_obj_get(object, "init_index")),
        .init_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(object, "init_count")),
        .increment_index = (uint32_t)yyjson_get_uint(yyjson_obj_get(object, "increment_index")),
        .total_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(object, "total_count")),
        .region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(object, "region_index")),
        .road_network_type = (uint8_t)yyjson_get_uint(yyjson_obj_get(object, "road_network_type")),
    };
}

void serdeNavWriteModuleMetaExtraFields(yyjson_mut_doc* doc, yyjson_mut_val* root, const char* type_name) {
    const DominoNavDataSegmentVec* segment_list =
        strcmp(type_name, "fork_road") == 0 ? &domino_all_fork_road_nav_data_segment_list : &domino_all_road_nav_data_segment_list;

    yyjson_mut_val* array = yyjson_mut_obj_add_arr(doc, root, "data_segment");
    if (!array) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate nav data segment array: type=%s", type_name);
    }
    for (size_t index = 0U; index < kv_size(*segment_list); index++) {
        yyjson_mut_val* item = serializeNavDataSegment(doc, &kv_A(*segment_list, index));
        if (!yyjson_mut_arr_append(array, item)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to serialize nav data segment: type=%s, index=%zu", type_name, index);
        }
    }
}

/** @brief 从可信元数据恢复分段；保留本次容器容量和分配大小检查。 */
void serdeNavReadModuleMetaExtraFields(yyjson_val* root, const char* type_name) {
    DominoNavDataSegmentVec* out_segment_list =
        strcmp(type_name, "fork_road") == 0 ? &domino_all_fork_road_nav_data_segment_list : &domino_all_road_nav_data_segment_list;

    yyjson_val* array = yyjson_obj_get(root, "data_segment");
    size_t segment_count = yyjson_arr_size(array);
    if (segment_count > DOMINO_NAV_DATA_SEGMENT_KEY_COUNT) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Nav segment count exceeds capacity: type=%s, count=%zu", type_name, segment_count);
    }
    if (segment_count > SIZE_MAX / sizeof(DominoNavDataSegment)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "Nav segment allocation size overflows: type=%s, count=%zu", type_name, segment_count);
    }

    size_t index;
    size_t count;
    yyjson_val* item;
    yyjson_arr_foreach(array, index, count, item) {
        DominoNavDataSegment segment;
        deserializeNavDataSegment(item, &segment);
        kv_push(DominoNavDataSegment, *out_segment_list, segment);
    }
}

static yyjson_mut_val* serializeForkRoad(yyjson_mut_doc* doc, const DominoForkRoad* fork_road) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    if (!obj || !yyjson_mut_obj_add_uint(doc, obj, "id", fork_road->id) || !yyjson_mut_obj_add_sint(doc, obj, "position_x", fork_road->position_x) ||
        !yyjson_mut_obj_add_sint(doc, obj, "position_y", fork_road->position_y) ||
        !yyjson_mut_obj_add_sint(doc, obj, "position_z", fork_road->position_z) || !yyjson_mut_obj_add_sint(doc, obj, "weight", fork_road->weight) ||
        !yyjson_mut_obj_add_uint(doc, obj, "region_index", fork_road->region_index) ||
        !yyjson_mut_obj_add_uint(doc, obj, "status", fork_road->status) || !yyjson_mut_obj_add_uint(doc, obj, "type", fork_road->type)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    yyjson_mut_val* num_array = serdeWriteU8FixedArray(doc, fork_road->num, 2);
    if (!yyjson_mut_obj_add_val(doc, obj, "num", num_array)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    return obj;
}

static void deserializeForkRoad(yyjson_val* obj, DominoForkRoad* out_fork_road) {
    out_fork_road->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_fork_road->position_x = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_x"));
    out_fork_road->position_y = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_y"));
    out_fork_road->position_z = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_z"));
    out_fork_road->weight = (domino_nav_weight_atom_t)yyjson_get_uint(yyjson_obj_get(obj, "weight"));
    out_fork_road->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_fork_road->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    out_fork_road->type = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    serdeReadU8Array(yyjson_obj_get(obj, "num"), out_fork_road->num, 2);
}

/** @brief 写出转折点；可信加载及业务边界保证与数量对应的内联或堆存储有效。 */
static yyjson_mut_val* serdeWriteCornerPoints(yyjson_mut_doc* doc, const DominoRoad* road) {
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    if (!arr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    uint16_t count = road->corner_point_count;
    if (count > MIN_CORNER_POINTS_PER_ROAD) {
        for (uint16_t i = 0; i < count; i++) {
            yyjson_mut_val* point_arr = yyjson_mut_arr(doc);
            if (!point_arr || !yyjson_mut_arr_add_sint(doc, point_arr, road->corner_point_ptr[i][0]) ||
                !yyjson_mut_arr_add_sint(doc, point_arr, road->corner_point_ptr[i][1]) ||
                !yyjson_mut_arr_add_sint(doc, point_arr, road->corner_point_ptr[i][2]) || !yyjson_mut_arr_append(arr, point_arr)) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
            }
        }
        return arr;
    }

    for (uint16_t i = 0; i < count; i++) {
        yyjson_mut_val* point_arr = yyjson_mut_arr(doc);
        if (!point_arr || !yyjson_mut_arr_add_sint(doc, point_arr, road->corner_points[0][i]) ||
            !yyjson_mut_arr_add_sint(doc, point_arr, road->corner_points[1][i]) ||
            !yyjson_mut_arr_add_sint(doc, point_arr, road->corner_points[2][i]) || !yyjson_mut_arr_append(arr, point_arr)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
        }
    }
    return arr;
}

/** @brief 转折点数量来自可信存档；按该数量分配并写入对应的 union 存储。 */
static void serdeReadCornerPoints(yyjson_val* json_corner_points, DominoRoad* out_road) {
    const uint16_t count = out_road->corner_point_count;
    if (count > MIN_CORNER_POINTS_PER_ROAD) {
        out_road->corner_point_ptr = malloc(sizeof(*out_road->corner_point_ptr) * (size_t)count);
        if (!out_road->corner_point_ptr) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate road corner points: count=%u", count);
        }
    }
    for (uint16_t index = 0; index < count; index++) {
        yyjson_val* point = yyjson_arr_get(json_corner_points, index);
        for (size_t axis = 0; axis < 3; axis++) {
            int32_t coordinate = (int32_t)yyjson_get_sint(yyjson_arr_get(point, axis));
            if (count > MIN_CORNER_POINTS_PER_ROAD) {
                out_road->corner_point_ptr[index][axis] = coordinate;
            } else {
                out_road->corner_points[axis][index] = coordinate;
            }
        }
    }
}

static yyjson_mut_val* serializeRoad(yyjson_mut_doc* doc, const DominoRoad* road) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    if (!obj) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    yyjson_mut_val* corner_points = serdeWriteCornerPoints(doc, road);
    if (!yyjson_mut_obj_add_val(doc, obj, "corner_points", corner_points) || !yyjson_mut_obj_add_uint(doc, obj, "id", road->id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "distance", road->distance) ||
        !yyjson_mut_obj_add_uint(doc, obj, "corner_point_count", road->corner_point_count)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    yyjson_mut_val* time_array = serdeWriteU8FixedArray(doc, road->time, 4);
    if (!yyjson_mut_obj_add_val(doc, obj, "time", time_array) || !yyjson_mut_obj_add_uint(doc, obj, "cost", road->cost)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    yyjson_mut_val* speed_array = serdeWriteU8FixedArray(doc, road->speed, 4);
    if (!yyjson_mut_obj_add_val(doc, obj, "speed", speed_array)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    yyjson_mut_val* weight_array = serdeWriteU8FixedArray(doc, road->weight, 4);
    if (!yyjson_mut_obj_add_val(doc, obj, "weight", weight_array) || !yyjson_mut_obj_add_uint(doc, obj, "type", road->type) ||
        !yyjson_mut_obj_add_uint(doc, obj, "status", road->status) || !yyjson_mut_obj_add_uint(doc, obj, "region_index", road->region_index)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    yyjson_mut_val* num_array = serdeWriteU8FixedArray(doc, road->num, 2);
    if (!yyjson_mut_obj_add_val(doc, obj, "num", num_array)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    return obj;
}

static void deserializeRoad(yyjson_val* obj, DominoRoad* out_road) {
    out_road->id = (domino_road_id_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_road->distance = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "distance"));
    out_road->corner_point_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "corner_point_count"));
    serdeReadU8Array(yyjson_obj_get(obj, "time"), out_road->time, 4);
    out_road->cost = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "cost"));
    serdeReadU8Array(yyjson_obj_get(obj, "speed"), out_road->speed, 4);
    serdeReadU8Array(yyjson_obj_get(obj, "weight"), out_road->weight, 4);
    out_road->type = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    out_road->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    out_road->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    serdeReadU8Array(yyjson_obj_get(obj, "num"), out_road->num, 2);

    serdeReadCornerPoints(yyjson_obj_get(obj, "corner_points"), out_road);
}

static yyjson_mut_val* serializeRoadLine(yyjson_mut_doc* doc, const DominoRoadLine* road_line) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    if (!obj || !yyjson_mut_obj_add_uint(doc, obj, "id", road_line->id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }

    yyjson_mut_val* fork_ids = yyjson_mut_arr(doc);
    if (!fork_ids) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    for (uint16_t i = 0; i < road_line->fork_road_count; i++) {
        if (!yyjson_mut_arr_add_uint(doc, fork_ids, road_line->fork_road_id_list[i])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
        }
    }
    if (!yyjson_mut_obj_add_val(doc, obj, "fork_road_ids", fork_ids)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }

    yyjson_mut_val* start_location = serdeWriteSintVec2(doc, road_line->start_location.x, road_line->start_location.y);
    if (!yyjson_mut_obj_add_val(doc, obj, "start_location", start_location)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    yyjson_mut_val* target_location = serdeWriteSintVec2(doc, road_line->target_location.x, road_line->target_location.y);
    if (!yyjson_mut_obj_add_val(doc, obj, "target_location", target_location)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    yyjson_mut_val* current_location = serdeWriteSintVec2(doc, road_line->current_location.x, road_line->current_location.y);
    if (!yyjson_mut_obj_add_val(doc, obj, "current_location", current_location)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "start_fork_road_id", road_line->start_fork_road_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "target_fork_road_id", road_line->target_fork_road_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "current_fork_road_id", road_line->current_fork_road_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "used_time", road_line->used_time) ||
        !yyjson_mut_obj_add_uint(doc, obj, "remaining_time", road_line->remaining_time) ||
        !yyjson_mut_obj_add_uint(doc, obj, "passed_distance", road_line->passed_distance) ||
        !yyjson_mut_obj_add_uint(doc, obj, "remaining_distance", road_line->remaining_distance) ||
        !yyjson_mut_obj_add_uint(doc, obj, "consumed_cost", road_line->consumed_cost) ||
        !yyjson_mut_obj_add_uint(doc, obj, "remaining_cost", road_line->remaining_cost) ||
        !yyjson_mut_obj_add_uint(doc, obj, "fork_road_count", road_line->fork_road_count) ||
        !yyjson_mut_obj_add_uint(doc, obj, "road_index", road_line->road_index) || !yyjson_mut_obj_add_uint(doc, obj, "type", road_line->type) ||
        !yyjson_mut_obj_add_uint(doc, obj, "status", road_line->status) ||
        !yyjson_mut_obj_add_uint(doc, obj, "region_index", road_line->region_index)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    return obj;
}

static void deserializeRoadLine(yyjson_val* obj, DominoRoadLine* out_road_line) {
    out_road_line->id = (domino_road_line_id_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));

    uint64_t fork_count_u = yyjson_get_uint(yyjson_obj_get(obj, "fork_road_count"));
    if (fork_count_u > (uint64_t)MAX_FORK_ROAD_COUNT_PER_ROAD_LINE) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Road line fork count exceeds capacity: count=%llu", (unsigned long long)fork_count_u);
    }
    out_road_line->fork_road_count = (uint16_t)fork_count_u;

    yyjson_val* fork_ids = yyjson_obj_get(obj, "fork_road_ids");
    for (uint16_t index = 0; index < out_road_line->fork_road_count; index++) {
        out_road_line->fork_road_id_list[index] = (domino_fork_road_id_t)yyjson_get_uint(yyjson_arr_get(fork_ids, index));
    }

    serdeReadSintVec2FromJson(obj, "start_location", &out_road_line->start_location.x, &out_road_line->start_location.y);
    serdeReadSintVec2FromJson(obj, "target_location", &out_road_line->target_location.x, &out_road_line->target_location.y);
    serdeReadSintVec2FromJson(obj, "current_location", &out_road_line->current_location.x, &out_road_line->current_location.y);

    out_road_line->start_fork_road_id = (domino_fork_road_id_t)yyjson_get_uint(yyjson_obj_get(obj, "start_fork_road_id"));
    out_road_line->target_fork_road_id = (domino_fork_road_id_t)yyjson_get_uint(yyjson_obj_get(obj, "target_fork_road_id"));
    out_road_line->current_fork_road_id = (domino_fork_road_id_t)yyjson_get_uint(yyjson_obj_get(obj, "current_fork_road_id"));

    out_road_line->used_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "used_time"));
    out_road_line->remaining_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "remaining_time"));
    out_road_line->passed_distance = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "passed_distance"));
    out_road_line->remaining_distance = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "remaining_distance"));
    out_road_line->consumed_cost = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "consumed_cost"));
    out_road_line->remaining_cost = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "remaining_cost"));

    uint64_t road_index_u = yyjson_get_uint(yyjson_obj_get(obj, "road_index"));
    uint64_t type_u = yyjson_get_uint(yyjson_obj_get(obj, "type"));
    uint64_t status_u = yyjson_get_uint(yyjson_obj_get(obj, "status"));
    uint64_t region_u = yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_road_line->road_index = (uint8_t)road_index_u;
    out_road_line->type = (domino_type_t)type_u;
    out_road_line->status = (domino_status_t)status_u;
    out_road_line->region_index = (uint8_t)region_u;
}

STORAGE_DEFINE_SHARDED_SIMPLE(ForkRoad, "fork_road", DominoForkRoad, domino_all_fork_road_list, dominoForkRoadIdMap, dominoForkRoadIdMap_put,
                              serializeForkRoad, deserializeForkRoad, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)

STORAGE_DEFINE_SHARDED_SIMPLE(Road, "road", DominoRoad, domino_all_road_list, dominoRoadIdMap, dominoRoadIdMap_put, serializeRoad, deserializeRoad,
                              DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)

STORAGE_DEFINE_SHARDED_SIMPLE(RoadLine, "road_line", DominoRoadLine, domino_all_road_line_list, dominoRoadLineIdMap, dominoRoadLineIdMap_put,
                              serializeRoadLine, deserializeRoadLine, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)
