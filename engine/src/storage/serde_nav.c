#include <stdlib.h>
#include <string.h>

#include "../logger/entry.h"
#include "../nav/storage_view.h"
#include "io.h"
#include "klib/kvec.h"
#include "serde.h"
#include "serde_registry.h"

#define DOMINO_NAV_DATA_SEGMENT_KEY_COUNT ((size_t)UINT16_MAX + 1U)

/** @brief 仅保存分段布局；Floyd 矩阵和实体指针表在运行期构建。 */
static DOMINO_CODE serializeNavDataSegment(yyjson_mut_doc* doc, const DominoNavDataSegment* segment, yyjson_mut_val** out_object) {
    if (!doc || !segment || !out_object) {
        return ERR_INVALID_PARAM;
    }
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (!object || !yyjson_mut_obj_add_uint(doc, object, "init_index", segment->init_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "init_count", segment->init_count) ||
        !yyjson_mut_obj_add_uint(doc, object, "increment_index", segment->increment_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "total_count", segment->total_count) ||
        !yyjson_mut_obj_add_uint(doc, object, "region_index", segment->region_index) ||
        !yyjson_mut_obj_add_uint(doc, object, "road_network_type", segment->road_network_type)) {
        return ERR_MEMORY_ALLOC;
    }
    *out_object = object;
    return CODE_OK;
}

static DOMINO_CODE deserializeNavDataSegment(yyjson_val* object, DominoNavDataSegment* out_segment) {
    if (!object || !yyjson_is_obj(object) || !out_segment) {
        return ERR_INVALID_DATA;
    }

    yyjson_val* init_index_value = yyjson_obj_get(object, "init_index");
    yyjson_val* init_count_value = yyjson_obj_get(object, "init_count");
    yyjson_val* increment_index_value = yyjson_obj_get(object, "increment_index");
    yyjson_val* total_count_value = yyjson_obj_get(object, "total_count");
    yyjson_val* region_index_value = yyjson_obj_get(object, "region_index");
    yyjson_val* road_network_type_value = yyjson_obj_get(object, "road_network_type");
    if (!init_index_value || !yyjson_is_uint(init_index_value) || !init_count_value || !yyjson_is_uint(init_count_value) || !increment_index_value ||
        !yyjson_is_uint(increment_index_value) || !total_count_value || !yyjson_is_uint(total_count_value) || !region_index_value ||
        !yyjson_is_uint(region_index_value) || !road_network_type_value || !yyjson_is_uint(road_network_type_value)) {
        return ERR_INVALID_DATA;
    }

    uint64_t init_index = yyjson_get_uint(init_index_value);
    uint64_t init_count = yyjson_get_uint(init_count_value);
    uint64_t increment_index = yyjson_get_uint(increment_index_value);
    uint64_t total_count = yyjson_get_uint(total_count_value);
    uint64_t region_index = yyjson_get_uint(region_index_value);
    uint64_t road_network_type = yyjson_get_uint(road_network_type_value);
    if (init_index > UINT32_MAX || init_count > UINT32_MAX || increment_index > UINT32_MAX || total_count > UINT32_MAX || region_index > UINT8_MAX ||
        road_network_type > UINT8_MAX) {
        return ERR_OUT_OF_RANGE;
    }

    *out_segment = (DominoNavDataSegment){
        .init_index = (uint32_t)init_index,
        .init_count = (uint32_t)init_count,
        .increment_index = (uint32_t)increment_index,
        .total_count = (uint32_t)total_count,
        .region_index = (uint8_t)region_index,
        .road_network_type = (uint8_t)road_network_type,
    };
    return CODE_OK;
}

DOMINO_CODE serdeNavWriteModuleMetaExtraFields(yyjson_mut_doc* doc, yyjson_mut_val* root, const char* type_name) {
    if (!doc || !root || !type_name) {
        return ERR_INVALID_PARAM;
    }

    const DominoNavDataSegmentVec* segment_list = nullptr;
    if (strcmp(type_name, "fork_road") == 0) {
        segment_list = &domino_all_fork_road_nav_data_segment_list;
    } else if (strcmp(type_name, "road") == 0) {
        segment_list = &domino_all_road_nav_data_segment_list;
    } else {
        return CODE_OK;
    }

    yyjson_mut_val* array = yyjson_mut_obj_add_arr(doc, root, "data_segment");
    if (!array) {
        return ERR_MEMORY_ALLOC;
    }
    for (size_t index = 0U; index < kv_size(*segment_list); index++) {
        yyjson_mut_val* item = nullptr;
        DOMINO_CODE result = serializeNavDataSegment(doc, &kv_A(*segment_list, index), &item);
        if (result != CODE_OK || !yyjson_mut_arr_append(array, item)) {
            return result != CODE_OK ? result : ERR_MEMORY_ALLOC;
        }
    }
    return CODE_OK;
}

/** @brief 校验 JSON 结构和范围；与实体数组的一致性由 Nav 加载后校验。 */
DOMINO_CODE serdeNavReadModuleMetaExtraFields(yyjson_val* root, const char* type_name) {
    if (!root || !yyjson_is_obj(root) || !type_name) {
        return ERR_INVALID_PARAM;
    }

    DominoNavDataSegmentVec* out_segment_list = nullptr;
    if (strcmp(type_name, "fork_road") == 0) {
        out_segment_list = &domino_all_fork_road_nav_data_segment_list;
    } else if (strcmp(type_name, "road") == 0) {
        out_segment_list = &domino_all_road_nav_data_segment_list;
    } else {
        return CODE_OK;
    }
    if (kv_size(*out_segment_list) != 0U) {
        return ERR_GAME_STATE_INVALID;
    }

    yyjson_val* array = yyjson_obj_get(root, "data_segment");
    yyjson_val* total_count_value = yyjson_obj_get(root, "total_count");
    if (!array || !yyjson_is_arr(array) || !total_count_value || !yyjson_is_uint(total_count_value)) {
        return ERR_INVALID_JSON;
    }

    uint64_t entity_count = yyjson_get_uint(total_count_value);
    size_t segment_count = yyjson_arr_size(array);
    if (entity_count > UINT32_MAX || segment_count > DOMINO_NAV_DATA_SEGMENT_KEY_COUNT || segment_count > entity_count) {
        return ERR_OUT_OF_RANGE;
    }

    DominoNavDataSegmentVec parsed_segments;
    kv_init(parsed_segments);
    if (segment_count > 0U) {
        kv_resize(DominoNavDataSegment, parsed_segments, segment_count);
    }

    size_t index;
    size_t count;
    yyjson_val* item;
    yyjson_arr_foreach(array, index, count, item) {
        DominoNavDataSegment segment;
        DOMINO_CODE result = deserializeNavDataSegment(item, &segment);
        if (result != CODE_OK) {
            kv_destroy(parsed_segments);
            return result;
        }
        kv_push(DominoNavDataSegment, parsed_segments, segment);
    }

    kv_destroy(*out_segment_list);
    *out_segment_list = parsed_segments;
    return CODE_OK;
}

static yyjson_mut_val* serializeForkRoad(yyjson_mut_doc* doc, const DominoForkRoad* fork_road) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", fork_road->id);
    yyjson_mut_obj_add_sint(doc, obj, "position_x", fork_road->position_x);
    yyjson_mut_obj_add_sint(doc, obj, "position_y", fork_road->position_y);
    yyjson_mut_obj_add_sint(doc, obj, "position_z", fork_road->position_z);
    yyjson_mut_obj_add_sint(doc, obj, "weight", fork_road->weight);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", fork_road->region_index);
    yyjson_mut_obj_add_uint(doc, obj, "status", fork_road->status);
    yyjson_mut_obj_add_uint(doc, obj, "type", fork_road->type);
    yyjson_mut_obj_add_val(doc, obj, "num", serdeWriteU8FixedArray(doc, fork_road->num, 2));
    return obj;
}

static int deserializeForkRoad(yyjson_val* obj, DominoForkRoad* out_fork_road) {
    memset(out_fork_road, 0, sizeof(DominoForkRoad));
    uint32_t kind_u = 0;
    out_fork_road->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_fork_road->position_x = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_x"));
    out_fork_road->position_y = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_y"));
    out_fork_road->position_z = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_z"));
    out_fork_road->weight = (domino_nav_weight_atom_t)yyjson_get_uint(yyjson_obj_get(obj, "weight"));
    out_fork_road->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_fork_road->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    kind_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    if (kind_u > 255U) {
        return -1;
    }
    out_fork_road->type = (uint8_t)kind_u;
    serdeReadU8FixedArray(yyjson_obj_get(obj, "num"), out_fork_road->num, 2);
    return 0;
}

static yyjson_mut_val* serdeWriteCornerPoints(yyjson_mut_doc* doc, const DominoRoad* road) {
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    uint16_t count = road->corner_point_count;
    if (count == 0) {
        return arr;
    }

    if (count > MIN_CORNER_POINTS_PER_ROAD) {
        if (!road->corner_point_ptr) {
            return nullptr;
        }
        for (uint16_t i = 0; i < count; i++) {
            yyjson_mut_val* point_arr = yyjson_mut_arr(doc);
            yyjson_mut_arr_add_sint(doc, point_arr, road->corner_point_ptr[i][0]);
            yyjson_mut_arr_add_sint(doc, point_arr, road->corner_point_ptr[i][1]);
            yyjson_mut_arr_add_sint(doc, point_arr, road->corner_point_ptr[i][2]);
            yyjson_mut_arr_append(arr, point_arr);
        }
        return arr;
    }

    for (uint16_t i = 0; i < count; i++) {
        yyjson_mut_val* point_arr = yyjson_mut_arr(doc);
        yyjson_mut_arr_add_sint(doc, point_arr, road->corner_points[0][i]);
        yyjson_mut_arr_add_sint(doc, point_arr, road->corner_points[1][i]);
        yyjson_mut_arr_add_sint(doc, point_arr, road->corner_points[2][i]);
        yyjson_mut_arr_append(arr, point_arr);
    }
    return arr;
}

static int serdeReadCornerPoints(yyjson_val* json_corner_points, DominoRoad* out_road) {
    if (!json_corner_points || !yyjson_is_arr(json_corner_points)) {
        out_road->corner_point_count = 0;
        return 0;
    }

    size_t json_count = yyjson_arr_size(json_corner_points);
    uint16_t want = out_road->corner_point_count;
    uint16_t count = want;
    if (count > (uint16_t)json_count) {
        count = (uint16_t)json_count;
    }

    int32_t (*allocated)[3] = nullptr;
    if (count > MIN_CORNER_POINTS_PER_ROAD) {
        int32_t (*buf)[3] = (int32_t (*)[3])malloc(sizeof(*buf) * (size_t)count);
        if (!buf) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "malloc corner_point_ptr failed count=%u", (uint32_t)count);
            return -1;
        }
        out_road->corner_point_ptr = buf;
        allocated = buf;
    }

    size_t idx;
    size_t cnt;
    yyjson_val* point_val;
    uint16_t write_index = 0;
    yyjson_arr_foreach(json_corner_points, idx, cnt, point_val) {
        if (write_index >= count) {
            break;
        }
        if (!point_val || !yyjson_is_arr(point_val)) {
            continue;
        }
        yyjson_val* x_val = yyjson_arr_get(point_val, 0);
        yyjson_val* y_val = yyjson_arr_get(point_val, 1);
        yyjson_val* z_val = yyjson_arr_get(point_val, 2);
        int32_t point_x = (int32_t)yyjson_get_sint(x_val);
        int32_t point_y = (int32_t)yyjson_get_sint(y_val);
        int32_t point_z = (int32_t)yyjson_get_sint(z_val);

        if (count > MIN_CORNER_POINTS_PER_ROAD) {
            out_road->corner_point_ptr[write_index][0] = point_x;
            out_road->corner_point_ptr[write_index][1] = point_y;
            out_road->corner_point_ptr[write_index][2] = point_z;
        } else {
            out_road->corner_points[0][write_index] = point_x;
            out_road->corner_points[1][write_index] = point_y;
            out_road->corner_points[2][write_index] = point_z;
        }
        write_index++;
    }

    if (allocated && write_index <= MIN_CORNER_POINTS_PER_ROAD) {
        /* 有效点数缩至内联容量后切换 union；写入坐标后不能再写 corner_point_ptr。 */
        for (uint16_t i = 0; i < write_index; i++) {
            out_road->corner_points[0][i] = allocated[i][0];
            out_road->corner_points[1][i] = allocated[i][1];
            out_road->corner_points[2][i] = allocated[i][2];
        }
        free(allocated);
    }

    out_road->corner_point_count = write_index;
    return 0;
}

static yyjson_mut_val* serializeRoad(yyjson_mut_doc* doc, const DominoRoad* road) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);

    yyjson_mut_obj_add_val(doc, obj, "corner_points", serdeWriteCornerPoints(doc, road));
    yyjson_mut_obj_add_uint(doc, obj, "id", road->id);
    yyjson_mut_obj_add_uint(doc, obj, "distance", road->distance);
    yyjson_mut_obj_add_uint(doc, obj, "corner_point_count", road->corner_point_count);

    yyjson_mut_obj_add_val(doc, obj, "time", serdeWriteU8FixedArray(doc, road->time, 4));
    yyjson_mut_obj_add_uint(doc, obj, "cost", road->cost);
    yyjson_mut_obj_add_val(doc, obj, "speed", serdeWriteU8FixedArray(doc, road->speed, 4));
    yyjson_mut_obj_add_val(doc, obj, "weight", serdeWriteU8FixedArray(doc, road->weight, 4));

    yyjson_mut_obj_add_uint(doc, obj, "type", road->type);
    yyjson_mut_obj_add_uint(doc, obj, "status", road->status);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", road->region_index);
    yyjson_mut_obj_add_val(doc, obj, "num", serdeWriteU8FixedArray(doc, road->num, 2));
    return obj;
}

static int deserializeRoad(yyjson_val* obj, DominoRoad* out_road) {
    memset(out_road, 0, sizeof(DominoRoad));
    uint32_t kind_u = 0;
    out_road->id = (domino_road_id_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_road->distance = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "distance"));
    out_road->corner_point_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "corner_point_count"));
    serdeReadU8FixedArray(yyjson_obj_get(obj, "time"), out_road->time, 4);
    out_road->cost = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "cost"));
    serdeReadU8FixedArray(yyjson_obj_get(obj, "speed"), out_road->speed, 4);
    serdeReadU8FixedArray(yyjson_obj_get(obj, "weight"), out_road->weight, 4);
    kind_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    if (kind_u > 255U) {
        return -1;
    }
    out_road->type = (uint8_t)kind_u;
    out_road->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    out_road->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    serdeReadU8FixedArray(yyjson_obj_get(obj, "num"), out_road->num, 2);

    return serdeReadCornerPoints(yyjson_obj_get(obj, "corner_points"), out_road);
}

static yyjson_mut_val* serializeRoadLine(yyjson_mut_doc* doc, const DominoRoadLine* road_line) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", road_line->id);

    yyjson_mut_val* fork_ids = yyjson_mut_arr(doc);
    for (uint16_t i = 0; i < road_line->fork_road_count; i++) {
        yyjson_mut_arr_add_uint(doc, fork_ids, road_line->fork_road_id_list[i]);
    }
    yyjson_mut_obj_add_val(doc, obj, "fork_road_ids", fork_ids);

    yyjson_mut_obj_add_val(doc, obj, "start_location", serdeWriteSintVec2(doc, road_line->start_location.x, road_line->start_location.y));
    yyjson_mut_obj_add_val(doc, obj, "target_location", serdeWriteSintVec2(doc, road_line->target_location.x, road_line->target_location.y));
    yyjson_mut_obj_add_val(doc, obj, "current_location", serdeWriteSintVec2(doc, road_line->current_location.x, road_line->current_location.y));

    yyjson_mut_obj_add_uint(doc, obj, "start_fork_road_id", road_line->start_fork_road_id);
    yyjson_mut_obj_add_uint(doc, obj, "target_fork_road_id", road_line->target_fork_road_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_fork_road_id", road_line->current_fork_road_id);

    yyjson_mut_obj_add_uint(doc, obj, "used_time", road_line->used_time);
    yyjson_mut_obj_add_uint(doc, obj, "remaining_time", road_line->remaining_time);
    yyjson_mut_obj_add_uint(doc, obj, "passed_distance", road_line->passed_distance);
    yyjson_mut_obj_add_uint(doc, obj, "remaining_distance", road_line->remaining_distance);
    yyjson_mut_obj_add_uint(doc, obj, "consumed_cost", road_line->consumed_cost);
    yyjson_mut_obj_add_uint(doc, obj, "remaining_cost", road_line->remaining_cost);
    yyjson_mut_obj_add_uint(doc, obj, "fork_road_count", road_line->fork_road_count);

    yyjson_mut_obj_add_uint(doc, obj, "road_index", road_line->road_index);
    yyjson_mut_obj_add_uint(doc, obj, "type", road_line->type);
    yyjson_mut_obj_add_uint(doc, obj, "status", road_line->status);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", road_line->region_index);
    return obj;
}

static int deserializeRoadLine(yyjson_val* obj, DominoRoadLine* out_road_line) {
    memset(out_road_line, 0, sizeof(DominoRoadLine));

    out_road_line->id = (domino_road_line_id_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));

    uint64_t fork_count_u = yyjson_get_uint(yyjson_obj_get(obj, "fork_road_count"));
    if (fork_count_u > (uint64_t)MAX_FORK_ROAD_COUNT_PER_ROAD_LINE) {
        return -1;
    }
    out_road_line->fork_road_count = (uint16_t)fork_count_u;

    yyjson_val* fork_ids = yyjson_obj_get(obj, "fork_road_ids");
    if (out_road_line->fork_road_count > 0) {
        if (!fork_ids || !yyjson_is_arr(fork_ids) || yyjson_arr_size(fork_ids) != (size_t)out_road_line->fork_road_count) {
            return -1;
        }
        size_t idx;
        size_t cnt;
        yyjson_val* id_val;
        uint16_t write_i = 0;
        yyjson_arr_foreach(fork_ids, idx, cnt, id_val) {
            if (write_i >= out_road_line->fork_road_count) {
                break;
            }
            out_road_line->fork_road_id_list[write_i] = (domino_fork_road_id_t)yyjson_get_uint(id_val);
            write_i++;
        }
        if (write_i != out_road_line->fork_road_count) {
            return -1;
        }
    } else if (fork_ids) {
        if (!yyjson_is_arr(fork_ids) || yyjson_arr_size(fork_ids) != 0) {
            return -1;
        }
    }

    if (serdeReadSintVec2FromJson(obj, "start_location", &out_road_line->start_location.x, &out_road_line->start_location.y) != 0) {
        return -1;
    }
    if (serdeReadSintVec2FromJson(obj, "target_location", &out_road_line->target_location.x, &out_road_line->target_location.y) != 0) {
        return -1;
    }
    if (serdeReadSintVec2FromJson(obj, "current_location", &out_road_line->current_location.x, &out_road_line->current_location.y) != 0) {
        return -1;
    }

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
    if (road_index_u > 255U || type_u > 255U || status_u > 255U || region_u > 255U) {
        return -1;
    }
    out_road_line->road_index = (uint8_t)road_index_u;
    out_road_line->type = (domino_type_t)type_u;
    out_road_line->status = (domino_status_t)status_u;
    out_road_line->region_index = (uint8_t)region_u;

    for (uint16_t trail_i = out_road_line->fork_road_count; trail_i < MAX_FORK_ROAD_COUNT_PER_ROAD_LINE; trail_i++) {
        out_road_line->fork_road_id_list[trail_i] = 0;
    }
    return 0;
}

STORAGE_DEFINE_SHARDED_SIMPLE(ForkRoad, "fork_road", DominoForkRoad, domino_all_fork_road_list, dominoForkRoadIdMap, dominoForkRoadIdMap_put,
                              dominoForkRoadIdMap_resize, serializeForkRoad, deserializeForkRoad, DOMINO_FORK_ROAD_VEC_RESERVE_CAPACITY_MIN,
                              DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)

STORAGE_DEFINE_SHARDED_SIMPLE(Road, "road", DominoRoad, domino_all_road_list, dominoRoadIdMap, dominoRoadIdMap_put, dominoRoadIdMap_resize,
                              serializeRoad, deserializeRoad, DOMINO_ROAD_VEC_RESERVE_CAPACITY_MIN, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)

STORAGE_DEFINE_SHARDED_SIMPLE(RoadLine, "road_line", DominoRoadLine, domino_all_road_line_list, dominoRoadLineIdMap, dominoRoadLineIdMap_put,
                              dominoRoadLineIdMap_resize, serializeRoadLine, deserializeRoadLine, DOMINO_ROAD_LINE_VEC_RESERVE_CAPACITY_MIN,
                              DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)
