#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../entry.h"
#include "../host/entry.h"
#include "domino_shared_common.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

static yyjson_mut_val* serializeRegion(yyjson_mut_doc* doc, const DominoRegion* region) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    if (!obj) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    if (!yyjson_mut_obj_add_uint(doc, obj, "human_count", atomic_load(&region->human_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "org_count", atomic_load(&region->org_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "country_count", atomic_load(&region->country_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "city_count", atomic_load(&region->city_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "island_count", atomic_load(&region->island_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "building_count", atomic_load(&region->building_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "asset_count", atomic_load(&region->asset_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "fork_road_count", atomic_load(&region->fork_road_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "movable_object_count", atomic_load(&region->movable_object_count)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "type", region->type) || !yyjson_mut_obj_add_uint(doc, obj, "status", region->status)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    return obj;
}

static void deserializeRegion(yyjson_val* obj, DominoRegion* out_region) {
    if (!yyjson_is_obj(obj)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Region data must be a JSON object");
    }
    const struct RegionCountField {
        const char* key_str;
        _Atomic uint32_t* count_ptr;
    } count_fields[] = {
        {"human_count", &out_region->human_count},
        {"org_count", &out_region->org_count},
        {"country_count", &out_region->country_count},
        {"city_count", &out_region->city_count},
        {"island_count", &out_region->island_count},
        {"building_count", &out_region->building_count},
        {"asset_count", &out_region->asset_count},
        {"fork_road_count", &out_region->fork_road_count},
        {"movable_object_count", &out_region->movable_object_count},
    };
    for (size_t index = 0; index < sizeof(count_fields) / sizeof(count_fields[0]); index++) {
        yyjson_val* count_val = yyjson_obj_get(obj, count_fields[index].key_str);
        if (!yyjson_is_uint(count_val)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Region count must be an unsigned integer: field=%s", count_fields[index].key_str);
        }
        const uint64_t count = yyjson_get_uint(count_val);
        if (count > UINT32_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "Region count exceeds its numeric range: field=%s", count_fields[index].key_str);
        }
        atomic_store(count_fields[index].count_ptr, (uint32_t)count);
    }
    uint64_t type_u = yyjson_get_uint(yyjson_obj_get(obj, "type"));
    uint64_t status_u = yyjson_get_uint(yyjson_obj_get(obj, "status"));
    if (type_u > 255U || status_u > 255U) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "Region type or status exceeds its numeric range");
    }
    out_region->type = (uint8_t)type_u;
    out_region->status = (uint8_t)status_u;
}

void serdeSaveRegion(const char* save_path) {
    if (g_domino_region_count > DOMINO_MAX_REGION_COUNT) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Region count exceeds capacity: count=%u, capacity=%u", g_domino_region_count,
                               (unsigned)DOMINO_MAX_REGION_COUNT);
    }
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate region JSON document");
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    if (!root) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate region JSON root");
    }
    yyjson_mut_doc_set_root(doc, root);
    if (!yyjson_mut_obj_add_uint(doc, root, "region_count", g_domino_region_count)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to serialize region count");
    }

    yyjson_mut_val* regions = yyjson_mut_obj_add_arr(doc, root, "regions");
    if (!regions) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate region JSON array");
    }
    for (uint32_t region_index = 0; region_index < g_domino_region_count; region_index++) {
        if (!yyjson_mut_arr_append(regions, serializeRegion(doc, &g_domino_region_list[region_index]))) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to serialize region: index=%u", region_index);
        }
    }

    storageWriteShardFile("regions", 0, save_path, doc);
    yyjson_mut_doc_free(doc);
}

void serdeLoadRegion(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    /* 元数据层已按 regions 注册项确认单分片；地域容量仍在此处校验。 */
    (void)shard_count;
    if (total_count > (uint32_t)DOMINO_MAX_REGION_COUNT) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Saved region count exceeds capacity: count=%u, capacity=%u", total_count,
                               (unsigned)DOMINO_MAX_REGION_COUNT);
    }

    yyjson_doc* doc = storageReadShardFile("regions", 0, save_path);
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Region shard root must be a JSON object");
    }

    uint64_t region_count = yyjson_get_uint(yyjson_obj_get(root, "region_count"));
    if (region_count != total_count) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Region count does not match module metadata");
    }

    yyjson_val* regions = yyjson_obj_get(root, "regions");
    if (!yyjson_is_arr(regions)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Region shard regions field must be a JSON array");
    }
    size_t regions_size = yyjson_arr_size(regions);
    if (region_count != regions_size) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Region array length does not match saved region count");
    }

    /* host 初始化已清空全部地域；这里逐项原子写入有效槽。 */
    g_domino_region_count = total_count;
    size_t array_index;
    size_t array_count;
    yyjson_val* region_value;
    yyjson_arr_foreach(regions, array_index, array_count, region_value) {
        deserializeRegion(region_value, &g_domino_region_list[array_index]);
    }

    yyjson_doc_free(doc);
}

static yyjson_mut_val* serializeMovableObject(yyjson_mut_doc* doc, const DominoMovableObject* movable_object) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    if (!obj) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    if (!yyjson_mut_obj_add_uint(doc, obj, "id", movable_object->id) ||
        !yyjson_mut_obj_add_val(doc, obj, "position",
                                serdeWriteSintVec3(doc, movable_object->position_x, movable_object->position_y, movable_object->position_z)) ||
        !yyjson_mut_obj_add_val(doc, obj, "direction", serdeWriteFloatArray(doc, movable_object->direction, 3)) ||
        !yyjson_mut_obj_add_uint(doc, obj, "speed", movable_object->speed) ||
        !yyjson_mut_obj_add_sint(doc, obj, "acceleration", movable_object->acceleration) ||
        !yyjson_mut_obj_add_uint(doc, obj, "space_id", movable_object->space_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "space_area_id", movable_object->space_area_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    return obj;
}

static void deserializeMovableObject(yyjson_val* obj, DominoMovableObject* out_movable_object) {
    out_movable_object->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    serdeReadSintVec3FromJson(obj, "position", &out_movable_object->position_x, &out_movable_object->position_y, &out_movable_object->position_z);
    serdeReadFloatVec3FromJson(obj, "direction", out_movable_object->direction);
    out_movable_object->speed = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "speed"));
    out_movable_object->acceleration = (int8_t)yyjson_get_sint(yyjson_obj_get(obj, "acceleration"));
    out_movable_object->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_movable_object->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));
}

STORAGE_DEFINE_SHARDED_SIMPLE(MovableObject, "movable_object", DominoMovableObject, domino_all_movable_object_list, dominoMovableObjectIdMap,
                              dominoMovableObjectIdMap_put, serializeMovableObject, deserializeMovableObject, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)

static yyjson_mut_val* serializeIsland(yyjson_mut_doc* doc, const DominoIsland* island) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    if (!obj) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    if (!yyjson_mut_obj_add_uint(doc, obj, "id", island->id) || !yyjson_mut_obj_add_uint(doc, obj, "type", island->type) ||
        !yyjson_mut_obj_add_uint(doc, obj, "status", island->status) || !yyjson_mut_obj_add_uint(doc, obj, "region_index", island->region_index)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Failed to allocate entity JSON data");
    }
    return obj;
}

static void deserializeIsland(yyjson_val* obj, DominoIsland* out_island) {
    out_island->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_island->type = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    out_island->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    out_island->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
}

STORAGE_DEFINE_SHARDED_SIMPLE(Island, "island", DominoIsland, domino_all_island_list, dominoIslandIdMap, dominoIslandIdMap_put, serializeIsland,
                              deserializeIsland, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)
