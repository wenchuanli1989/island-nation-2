#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../entry.h"
#include "../host/entry.h"
#include "../logger/entry.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

static yyjson_mut_val* serializeRegion(yyjson_mut_doc* doc, const DominoRegion* region) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_sint(doc, obj, "human_count", atomic_load(&region->human_count));
    yyjson_mut_obj_add_sint(doc, obj, "org_count", atomic_load(&region->org_count));
    yyjson_mut_obj_add_sint(doc, obj, "country_count", atomic_load(&region->country_count));
    yyjson_mut_obj_add_sint(doc, obj, "city_count", atomic_load(&region->city_count));
    yyjson_mut_obj_add_sint(doc, obj, "island_count", atomic_load(&region->island_count));
    yyjson_mut_obj_add_sint(doc, obj, "building_count", atomic_load(&region->building_count));
    yyjson_mut_obj_add_sint(doc, obj, "asset_count", atomic_load(&region->asset_count));
    yyjson_mut_obj_add_sint(doc, obj, "fork_road_count", atomic_load(&region->fork_road_count));
    yyjson_mut_obj_add_uint(doc, obj, "type", region->type);
    yyjson_mut_obj_add_uint(doc, obj, "status", region->status);
    return obj;
}

static int deserializeRegion(yyjson_val* obj, DominoRegion* out_region) {
    if (!obj || !yyjson_is_obj(obj)) {
        return -1;
    }
    atomic_store(&out_region->human_count, (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "human_count")));
    atomic_store(&out_region->org_count, (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "org_count")));
    atomic_store(&out_region->country_count, (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "country_count")));
    atomic_store(&out_region->city_count, (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "city_count")));
    atomic_store(&out_region->island_count, (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "island_count")));
    atomic_store(&out_region->building_count, (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "building_count")));
    atomic_store(&out_region->asset_count, (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "asset_count")));
    atomic_store(&out_region->fork_road_count, (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "fork_road_count")));
    uint32_t type_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    uint32_t status_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    if (type_u > 255U || status_u > 255U) {
        return -1;
    }
    out_region->type = (uint8_t)type_u;
    out_region->status = (uint8_t)status_u;
    return 0;
}

DOMINO_CODE serdeSaveRegion(const char* save_path) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    if (g_domino_region_count > DOMINO_MAX_REGION_COUNT) {
        dominoAssertErrorCode(ERR_INVALID_DATA, true, __FILE__, __LINE__);
    }
    yyjson_mut_obj_add_uint(doc, root, "region_count", g_domino_region_count);

    yyjson_mut_val* regions = yyjson_mut_obj_add_arr(doc, root, "regions");
    for (uint32_t region_index = 0; region_index < g_domino_region_count; region_index++) {
        yyjson_mut_arr_append(regions, serializeRegion(doc, &g_domino_region_list[region_index]));
    }

    DOMINO_CODE result = storageWriteShardFile("regions", 0, save_path, doc);
    yyjson_mut_doc_free(doc);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "write regions.json failed");
        return result;
    }
    return result;
}

DOMINO_CODE serdeLoadRegion(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    if (shard_count != 1U || total_count > (uint32_t)DOMINO_MAX_REGION_COUNT) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid regions meta: total_count=%u shard_count=%u",
                          total_count, shard_count);
        return ERR_INVALID_DATA;
    }

    DOMINO_CODE result = CODE_OK;
    yyjson_doc* doc = storageReadShardFile("regions", 0, save_path, &result);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to read regions.json: %s",
                          dominoErrorCodeToString(result));
        return result;
    }
    if (!doc) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid regions.json: invalid json");
        return ERR_INVALID_JSON;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid regions.json: root is not object");
        yyjson_doc_free(doc);
        return ERR_SYSTEM;
    }

    uint32_t region_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(root, "region_count"));
    if (region_count != total_count) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid regions.json: region_count(%u) != meta total_count(%u)",
                          region_count, total_count);
        yyjson_doc_free(doc);
        return ERR_INVALID_DATA;
    }

    yyjson_val* regions = yyjson_obj_get(root, "regions");
    if (!regions || !yyjson_is_arr(regions)) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid regions.json: missing regions array");
        yyjson_doc_free(doc);
        return ERR_SYSTEM;
    }
    size_t regions_size = yyjson_arr_size(regions);
    if (region_count != (uint32_t)regions_size) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid regions.json: region_count(%u) != regions.size(%zu)",
                          region_count, regions_size);
        yyjson_doc_free(doc);
        return ERR_SYSTEM;
    }
    if (regions_size > (size_t)DOMINO_MAX_REGION_COUNT) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                          "invalid regions.json: regions.size(%zu) > DOMINO_MAX_REGION_COUNT(%u)", regions_size, (uint32_t)DOMINO_MAX_REGION_COUNT);
        yyjson_doc_free(doc);
        return ERR_SYSTEM;
    }

    /* host 初始化已清空全部地域；这里逐项原子写入有效槽。 */
    g_domino_region_count = region_count;
    size_t array_index;
    size_t array_count;
    yyjson_val* region_value;
    yyjson_arr_foreach(regions, array_index, array_count, region_value) {
        if (array_index >= (size_t)g_domino_region_count) {
            break;
        }
        if (deserializeRegion(region_value, &g_domino_region_list[array_index]) != 0) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid regions.json: bad region entry index=%u",
                              (uint32_t)array_index);
            yyjson_doc_free(doc);
            return ERR_INVALID_DATA;
        }
    }

    yyjson_doc_free(doc);
    return CODE_OK;
}

static yyjson_mut_val* serializeMovableObject(yyjson_mut_doc* doc, const DominoMovableObject* movable_object) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", movable_object->id);
    yyjson_mut_obj_add_val(doc, obj, "position",
                           serdeWriteSintVec3(doc, movable_object->position_x, movable_object->position_y, movable_object->position_z));
    yyjson_mut_obj_add_val(doc, obj, "direction", serdeWriteFloatArray(doc, movable_object->direction, 3));
    yyjson_mut_obj_add_uint(doc, obj, "speed", movable_object->speed);
    yyjson_mut_obj_add_sint(doc, obj, "acceleration", movable_object->acceleration);
    yyjson_mut_obj_add_uint(doc, obj, "space_id", movable_object->space_id);
    yyjson_mut_obj_add_uint(doc, obj, "space_area_id", movable_object->space_area_id);
    return obj;
}

static int deserializeMovableObject(yyjson_val* obj, DominoMovableObject* out_movable_object) {
    memset(out_movable_object, 0, sizeof(DominoMovableObject));
    out_movable_object->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    if (serdeReadSintVec3FromJson(obj, "position", &out_movable_object->position_x, &out_movable_object->position_y,
                                  &out_movable_object->position_z) != 0) {
        return -1;
    }
    if (serdeReadFloatVec3FromJson(obj, "direction", out_movable_object->direction) != 0) {
        return -1;
    }
    out_movable_object->speed = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "speed"));
    out_movable_object->acceleration = (int8_t)yyjson_get_sint(yyjson_obj_get(obj, "acceleration"));
    out_movable_object->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_movable_object->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));
    return 0;
}

STORAGE_DEFINE_SHARDED_SIMPLE(MovableObject, "movable_object", DominoMovableObject, domino_all_movable_object_list, dominoMovableObjectIdMap,
                              dominoMovableObjectIdMap_put, dominoMovableObjectIdMap_resize, serializeMovableObject, deserializeMovableObject,
                              HOST_MOVABLE_OBJECT_VEC_RESERVE_CAPACITY_MIN, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)

static yyjson_mut_val* serializeIsland(yyjson_mut_doc* doc, const DominoIsland* island) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", island->id);
    yyjson_mut_obj_add_uint(doc, obj, "type", island->type);
    yyjson_mut_obj_add_uint(doc, obj, "status", island->status);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", island->region_index);
    return obj;
}

static int deserializeIsland(yyjson_val* obj, DominoIsland* out_island) {
    memset(out_island, 0, sizeof(DominoIsland));
    out_island->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_island->type = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    out_island->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    out_island->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    return 0;
}

STORAGE_DEFINE_SHARDED_SIMPLE(Island, "island", DominoIsland, domino_all_island_list, dominoIslandIdMap, dominoIslandIdMap_put,
                              dominoIslandIdMap_resize, serializeIsland, deserializeIsland, HOST_ISLAND_VEC_RESERVE_CAPACITY_MIN,
                              DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)
