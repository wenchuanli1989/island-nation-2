#include <stdio.h>
#include <string.h>

#include "../host/entry.h"
#include "../logger/entry.h"
#include "../social/entry.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/*
 * social 中 org/building/city/country 都采用外层实体 + 可选 data 嵌套的布局。
 * 保存时必须校验 data_id、data 指针和全局 data_id map 指向同一份数据；加载时同步重建 data 向量和 map。
 */

static yyjson_mut_val* serializeOrgData(yyjson_mut_doc* doc, const DominoOrgData* org_data) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", org_data->data_id);
    yyjson_mut_obj_add_val(doc, obj, "member_id_list", serdeWriteU32Array(doc, org_data->member_id_list, DOMINO_MAX_MEMBER_COUNT_PER_ORG));
    yyjson_mut_obj_add_val(doc, obj, "own_asset_id_list", serdeWriteU32Array(doc, org_data->own_asset_id_list, DOMINO_MAX_ASSET_COUNT_PER_ORG));
    serdeWriteNameDescription(doc, obj, org_data->name_id, org_data->description_id);
    return obj;
}

static int deserializeOrgData(yyjson_val* obj, DominoOrgData* out_org_data) {
    memset(out_org_data, 0, sizeof(DominoOrgData));
    out_org_data->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    serdeReadU32Array(yyjson_obj_get(obj, "member_id_list"), out_org_data->member_id_list, DOMINO_MAX_MEMBER_COUNT_PER_ORG);
    serdeReadU32Array(yyjson_obj_get(obj, "own_asset_id_list"), out_org_data->own_asset_id_list, DOMINO_MAX_ASSET_COUNT_PER_ORG);
    if (serdeReadNameDescription(obj, &out_org_data->name_id, &out_org_data->description_id) != CODE_OK) {
        return -1;
    }
    return 0;
}

static yyjson_mut_val* serializeOrg(yyjson_mut_doc* doc, const DominoOrg* org) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", org->id);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", org->data_id);
    yyjson_mut_obj_add_uint(doc, obj, "boss_id", org->boss_id);
    yyjson_mut_obj_add_uint(doc, obj, "island_id", org->island_id);
    yyjson_mut_obj_add_uint(doc, obj, "city_id", org->city_id);
    yyjson_mut_obj_add_uint(doc, obj, "country_id", org->country_id);
    yyjson_mut_obj_add_uint(doc, obj, "building_id", org->building_id);
    yyjson_mut_obj_add_sint(doc, obj, "wealth", org->wealth);
    yyjson_mut_obj_add_uint(doc, obj, "founder_id", org->founder_id);
    yyjson_mut_obj_add_val(doc, obj, "key_member_id_list", serdeWriteU32Array(doc, org->key_member_id_list, DOMINO_MAX_KEY_MEMBER_COUNT_PER_ORG));
    yyjson_mut_obj_add_val(doc, obj, "micro_member_id_list",
                           serdeWriteU32Array(doc, org->micro_member_id_list, DOMINO_MAX_MICRO_MEMBER_COUNT_PER_ORG));
    yyjson_mut_obj_add_uint(doc, obj, "address_id", org->address_id);
    yyjson_mut_obj_add_uint(doc, obj, "outer_host_country_id", org->outer_host_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "total_member_count", org->total_member_count);
    yyjson_mut_obj_add_uint(doc, obj, "status", org->status);
    yyjson_mut_obj_add_uint(doc, obj, "type", org->type);
    yyjson_mut_obj_add_uint(doc, obj, "space_id", org->space_id);
    yyjson_mut_obj_add_uint(doc, obj, "space_area_id", org->space_area_id);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", org->region_index);
    yyjson_mut_obj_add_uint(doc, obj, "own_asset_list_count", org->own_asset_list_count);

    if (!org->data && org->data_id != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "org id=%u data_id=%u but data is null", org->id, org->data_id);
        return nullptr;
    }
    if (org->data && org->data_id == 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "org id=%u data is not null but data_id is 0", org->id);
        return nullptr;
    }
    if (org->data) {
        DominoOrgData* data_from_id = dominoGetOrgDataByID(org->data_id);
        if (!data_from_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "org id=%u data_id=%u not found", org->id, org->data_id);
            return nullptr;
        }
        if (data_from_id != org->data) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "org id=%u data_id=%u data pointer mismatch", org->id,
                              org->data_id);
            return nullptr;
        }
        yyjson_mut_obj_add_val(doc, obj, "data", serializeOrgData(doc, org->data));
    } else {
        yyjson_mut_obj_add_null(doc, obj, "data");
    }
    return obj;
}

static int deserializeOrg(yyjson_val* obj, DominoOrg* out_org) {
    memset(out_org, 0, sizeof(DominoOrg));
    uint32_t kind_u = 0;
    out_org->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_org->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    out_org->boss_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "boss_id"));
    out_org->island_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "island_id"));
    out_org->city_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "city_id"));
    out_org->country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "country_id"));
    out_org->building_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "building_id"));
    out_org->wealth = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "wealth"));
    out_org->founder_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "founder_id"));
    serdeReadU32Array(yyjson_obj_get(obj, "key_member_id_list"), out_org->key_member_id_list, DOMINO_MAX_KEY_MEMBER_COUNT_PER_ORG);
    serdeReadU32Array(yyjson_obj_get(obj, "micro_member_id_list"), out_org->micro_member_id_list, DOMINO_MAX_MICRO_MEMBER_COUNT_PER_ORG);
    out_org->address_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "address_id"));
    out_org->outer_host_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "outer_host_country_id"));
    out_org->total_member_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "total_member_count"));
    out_org->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    kind_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    if (kind_u > 255U) {
        return -1;
    }
    out_org->type = (uint8_t)kind_u;
    out_org->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_org->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));
    out_org->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_org->own_asset_list_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "own_asset_list_count"));

    yyjson_val* data_val = yyjson_obj_get(obj, "data");
    if (data_val && !yyjson_is_null(data_val)) {
        /* 先 push 到 data 向量，再把 data_id 映射到数组下标；重复 data_id 时回滚向量长度。 */
        DominoOrgData loaded_org_data = {0};
        if (deserializeOrgData(data_val, &loaded_org_data) != 0) {
            return -1;
        }
        if (out_org->data_id != loaded_org_data.data_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "org id=%u entity data_id=%u != nested data data_id=%u",
                              out_org->id, out_org->data_id, loaded_org_data.data_id);
            return -1;
        }
        size_t old_data_count = kv_size(domino_all_org_data_list);
        kv_push(DominoOrgData, domino_all_org_data_list, loaded_org_data);
        uint32_t data_arr_index = (uint32_t)old_data_count;
        int absent;
        khint_t data_id_map_slot = dominoOrgDataIdMap_put(dominoOrgDataIdMap, loaded_org_data.data_id, &absent);
        if (absent < 0) {
            domino_all_org_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to map org_data data_id=%u", loaded_org_data.data_id);
            return -1;
        }
        if (absent == 0) {
            domino_all_org_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate org_data data_id=%u", loaded_org_data.data_id);
            return -1;
        }
        kh_val(dominoOrgDataIdMap, data_id_map_slot) = data_arr_index;
        out_org->data = &kv_A(domino_all_org_data_list, data_arr_index);
    }
    return 0;
}

STORAGE_DEFINE_SHARDED_WITH_DATA(Org, "org", DominoOrg, domino_all_org_list, dominoOrgIdMap, dominoOrgIdMap_put, dominoOrgIdMap_resize, serializeOrg,
                                 deserializeOrg, DOMINO_SOCIAL_ORG_VEC_RESERVE_CAPACITY_MIN, DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA, DominoOrgData,
                                 domino_all_org_data_list, dominoOrgDataIdMap, dominoOrgDataIdMap_resize)

static yyjson_mut_val* serializeBuildingData(yyjson_mut_doc* doc, const DominoBuildingData* building_data) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", building_data->data_id);
    serdeWriteNameDescription(doc, obj, building_data->name_id, building_data->description_id);
    yyjson_mut_obj_add_val(doc, obj, "member_id_list", serdeWriteU32Array(doc, building_data->member_id_list, DOMINO_MAX_MEMBER_COUNT_PER_BUILDING));
    yyjson_mut_obj_add_val(doc, obj, "member_type_list",
                           serdeWriteU8Array(doc, building_data->member_type_list, DOMINO_MAX_MEMBER_COUNT_PER_BUILDING));
    return obj;
}

static int deserializeBuildingData(yyjson_val* obj, DominoBuildingData* out_building_data) {
    memset(out_building_data, 0, sizeof(DominoBuildingData));
    out_building_data->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    if (serdeReadNameDescription(obj, &out_building_data->name_id, &out_building_data->description_id) != CODE_OK) {
        return -1;
    }
    serdeReadU32Array(yyjson_obj_get(obj, "member_id_list"), out_building_data->member_id_list, DOMINO_MAX_MEMBER_COUNT_PER_BUILDING);
    serdeReadU8Array(yyjson_obj_get(obj, "member_type_list"), out_building_data->member_type_list, DOMINO_MAX_MEMBER_COUNT_PER_BUILDING);
    return 0;
}

static yyjson_mut_val* serializeBuilding(yyjson_mut_doc* doc, const DominoBuilding* building) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", building->id);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", building->data_id);
    yyjson_mut_obj_add_uint(doc, obj, "owner_org_id", building->owner_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "island_id", building->island_id);
    yyjson_mut_obj_add_uint(doc, obj, "city_id", building->city_id);
    yyjson_mut_obj_add_uint(doc, obj, "country_id", building->country_id);
    yyjson_mut_obj_add_sint(doc, obj, "wealth", building->wealth);
    yyjson_mut_obj_add_val(doc, obj, "micro_member_id_list",
                           serdeWriteU32Array(doc, building->micro_member_id_list, DOMINO_MAX_MICRO_MEMBER_COUNT_PER_BUILDING));
    yyjson_mut_obj_add_sint(doc, obj, "position_x", building->position_x);
    yyjson_mut_obj_add_sint(doc, obj, "position_y", building->position_y);
    yyjson_mut_obj_add_sint(doc, obj, "position_z", building->position_z);
    yyjson_mut_obj_add_uint(doc, obj, "outer_host_country_id", building->outer_host_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "address_id", building->address_id);
    yyjson_mut_obj_add_uint(doc, obj, "total_member_count", building->total_member_count);
    yyjson_mut_obj_add_uint(doc, obj, "status", building->status);
    yyjson_mut_obj_add_uint(doc, obj, "type", building->type);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", building->region_index);
    yyjson_mut_obj_add_uint(doc, obj, "owner_type", building->owner_type);
    yyjson_mut_obj_add_uint(doc, obj, "space_id", building->space_id);
    yyjson_mut_obj_add_uint(doc, obj, "space_area_id", building->space_area_id);
    yyjson_mut_obj_add_val(doc, obj, "micro_member_type_list",
                           serdeWriteU8Array(doc, building->micro_member_type_list, DOMINO_MAX_MICRO_MEMBER_COUNT_PER_BUILDING));

    if (!building->data && building->data_id != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "building id=%u data_id=%u but data is null", building->id,
                          building->data_id);
        return nullptr;
    }
    if (building->data && building->data_id == 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "building id=%u data is not null but data_id is 0", building->id);
        return nullptr;
    }
    if (building->data) {
        DominoBuildingData* data_from_id = dominoGetBuildingDataByID(building->data_id);
        if (!data_from_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "building id=%u data_id=%u not found", building->id,
                              building->data_id);
            return nullptr;
        }
        if (data_from_id != building->data) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "building id=%u data_id=%u data pointer mismatch",
                              building->id, building->data_id);
            return nullptr;
        }
        yyjson_mut_obj_add_val(doc, obj, "data", serializeBuildingData(doc, building->data));
    } else {
        yyjson_mut_obj_add_null(doc, obj, "data");
    }
    return obj;
}

static int deserializeBuilding(yyjson_val* obj, DominoBuilding* out_building) {
    memset(out_building, 0, sizeof(DominoBuilding));
    uint32_t kind_u = 0;
    out_building->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_building->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    out_building->owner_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "owner_org_id"));
    out_building->island_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "island_id"));
    out_building->city_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "city_id"));
    out_building->country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "country_id"));
    out_building->wealth = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "wealth"));
    serdeReadU32Array(yyjson_obj_get(obj, "micro_member_id_list"), out_building->micro_member_id_list, DOMINO_MAX_MICRO_MEMBER_COUNT_PER_BUILDING);
    out_building->position_x = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_x"));
    out_building->position_y = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_y"));
    out_building->position_z = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_z"));
    out_building->outer_host_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "outer_host_country_id"));
    out_building->address_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "address_id"));
    out_building->total_member_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "total_member_count"));
    out_building->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    kind_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    if (kind_u > 255U) {
        return -1;
    }
    out_building->type = (uint8_t)kind_u;
    out_building->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_building->owner_type = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "owner_type"));
    out_building->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_building->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));
    serdeReadU8Array(yyjson_obj_get(obj, "micro_member_type_list"), out_building->micro_member_type_list, DOMINO_MAX_MICRO_MEMBER_COUNT_PER_BUILDING);

    yyjson_val* data_val = yyjson_obj_get(obj, "data");
    if (data_val && !yyjson_is_null(data_val)) {
        DominoBuildingData loaded_building_data = {0};
        if (deserializeBuildingData(data_val, &loaded_building_data) != 0) {
            return -1;
        }
        if (out_building->data_id != loaded_building_data.data_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "building id=%u entity data_id=%u != nested data data_id=%u",
                              out_building->id, out_building->data_id, loaded_building_data.data_id);
            return -1;
        }
        size_t old_data_count = kv_size(domino_all_building_data_list);
        kv_push(DominoBuildingData, domino_all_building_data_list, loaded_building_data);
        uint32_t data_arr_index = (uint32_t)old_data_count;
        int absent;
        khint_t data_id_map_slot = dominoBuildingDataIdMap_put(dominoBuildingDataIdMap, loaded_building_data.data_id, &absent);
        if (absent < 0) {
            domino_all_building_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to map building_data data_id=%u",
                              loaded_building_data.data_id);
            return -1;
        }
        if (absent == 0) {
            domino_all_building_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate building_data data_id=%u",
                              loaded_building_data.data_id);
            return -1;
        }
        kh_val(dominoBuildingDataIdMap, data_id_map_slot) = data_arr_index;
        out_building->data = &kv_A(domino_all_building_data_list, data_arr_index);
    }
    return 0;
}

STORAGE_DEFINE_SHARDED_WITH_DATA(Building, "building", DominoBuilding, domino_all_building_list, dominoBuildingIdMap, dominoBuildingIdMap_put,
                                 dominoBuildingIdMap_resize, serializeBuilding, deserializeBuilding, DOMINO_SOCIAL_BUILDING_VEC_RESERVE_CAPACITY_MIN,
                                 DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA, DominoBuildingData, domino_all_building_data_list, dominoBuildingDataIdMap,
                                 dominoBuildingDataIdMap_resize)

static yyjson_mut_val* serializeCityData(yyjson_mut_doc* doc, const DominoCityData* city_data) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", city_data->data_id);
    serdeWriteNameDescription(doc, obj, city_data->name_id, city_data->description_id);
    yyjson_mut_obj_add_val(doc, obj, "org_id_list", serdeWriteU32Array(doc, city_data->org_id_list, DOMINO_MAX_ORG_COUNT_PER_CITY));
    yyjson_mut_obj_add_val(doc, obj, "building_id_list", serdeWriteU32Array(doc, city_data->building_id_list, DOMINO_MAX_BUILDING_COUNT_PER_CITY));
    yyjson_mut_obj_add_val(doc, obj, "moving_human_id_list",
                           serdeWriteU32Array(doc, city_data->moving_human_id_list, DOMINO_MAX_MOVING_HUMAN_COUNT_PER_CITY));
    yyjson_mut_obj_add_val(doc, obj, "moving_asset_id_list",
                           serdeWriteU32Array(doc, city_data->moving_asset_id_list, DOMINO_MAX_MOVING_ASSET_COUNT_PER_CITY));
    return obj;
}

static int deserializeCityData(yyjson_val* obj, DominoCityData* out_city_data) {
    memset(out_city_data, 0, sizeof(DominoCityData));
    out_city_data->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    if (serdeReadNameDescription(obj, &out_city_data->name_id, &out_city_data->description_id) != CODE_OK) {
        return -1;
    }
    serdeReadU32Array(yyjson_obj_get(obj, "org_id_list"), out_city_data->org_id_list, DOMINO_MAX_ORG_COUNT_PER_CITY);
    serdeReadU32Array(yyjson_obj_get(obj, "building_id_list"), out_city_data->building_id_list, DOMINO_MAX_BUILDING_COUNT_PER_CITY);
    serdeReadU32Array(yyjson_obj_get(obj, "moving_human_id_list"), out_city_data->moving_human_id_list, DOMINO_MAX_MOVING_HUMAN_COUNT_PER_CITY);
    serdeReadU32Array(yyjson_obj_get(obj, "moving_asset_id_list"), out_city_data->moving_asset_id_list, DOMINO_MAX_MOVING_ASSET_COUNT_PER_CITY);
    return 0;
}

static yyjson_mut_val* serializeCity(yyjson_mut_doc* doc, const DominoCity* city) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", city->id);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", city->data_id);
    yyjson_mut_obj_add_uint(doc, obj, "manager_id", city->manager_id);
    yyjson_mut_obj_add_uint(doc, obj, "country_id", city->country_id);
    yyjson_mut_obj_add_uint(doc, obj, "island_id", city->island_id);
    yyjson_mut_obj_add_sint(doc, obj, "wealth", city->wealth);
    yyjson_mut_obj_add_sint(doc, obj, "position_x", city->position_x);
    yyjson_mut_obj_add_sint(doc, obj, "position_y", city->position_y);
    yyjson_mut_obj_add_sint(doc, obj, "position_z", city->position_z);
    yyjson_mut_obj_add_uint(doc, obj, "outer_host_country_id", city->outer_host_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "human_count", city->human_count);
    yyjson_mut_obj_add_uint(doc, obj, "org_count", city->org_count);
    yyjson_mut_obj_add_uint(doc, obj, "building_count", city->building_count);
    yyjson_mut_obj_add_uint(doc, obj, "movable_human_count", city->movable_human_count);
    yyjson_mut_obj_add_uint(doc, obj, "movable_asset_count", city->movable_asset_count);
    yyjson_mut_obj_add_uint(doc, obj, "status", city->status);
    yyjson_mut_obj_add_uint(doc, obj, "type", city->type);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", city->region_index);
    yyjson_mut_obj_add_uint(doc, obj, "space_id", city->space_id);
    yyjson_mut_obj_add_uint(doc, obj, "space_area_id", city->space_area_id);

    if (!city->data && city->data_id != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "city id=%u data_id=%u but data is null", city->id,
                          city->data_id);
        return nullptr;
    }
    if (city->data && city->data_id == 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "city id=%u data is not null but data_id is 0", city->id);
        return nullptr;
    }
    if (city->data) {
        DominoCityData* data_from_id = dominoGetCityDataByID(city->data_id);
        if (!data_from_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "city id=%u data_id=%u not found", city->id, city->data_id);
            return nullptr;
        }
        if (data_from_id != city->data) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "city id=%u data_id=%u data pointer mismatch", city->id,
                              city->data_id);
            return nullptr;
        }
        yyjson_mut_obj_add_val(doc, obj, "data", serializeCityData(doc, city->data));
    } else {
        yyjson_mut_obj_add_null(doc, obj, "data");
    }
    return obj;
}

static int deserializeCity(yyjson_val* obj, DominoCity* out_city) {
    memset(out_city, 0, sizeof(DominoCity));
    uint32_t kind_u = 0;
    out_city->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_city->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    out_city->manager_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "manager_id"));
    out_city->country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "country_id"));
    out_city->island_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "island_id"));
    out_city->wealth = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "wealth"));
    out_city->position_x = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_x"));
    out_city->position_y = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_y"));
    out_city->position_z = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_z"));
    out_city->outer_host_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "outer_host_country_id"));
    out_city->human_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "human_count"));
    out_city->org_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "org_count"));
    out_city->building_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "building_count"));
    out_city->movable_human_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "movable_human_count"));
    out_city->movable_asset_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "movable_asset_count"));
    out_city->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    kind_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    if (kind_u > 255U) {
        return -1;
    }
    out_city->type = (uint8_t)kind_u;
    out_city->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_city->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_city->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));

    yyjson_val* data_val = yyjson_obj_get(obj, "data");
    if (data_val && !yyjson_is_null(data_val)) {
        DominoCityData loaded_city_data = {0};
        if (deserializeCityData(data_val, &loaded_city_data) != 0) {
            return -1;
        }
        if (out_city->data_id != loaded_city_data.data_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "city id=%u entity data_id=%u != nested data data_id=%u",
                              out_city->id, out_city->data_id, loaded_city_data.data_id);
            return -1;
        }
        size_t old_data_count = kv_size(domino_all_city_data_list);
        kv_push(DominoCityData, domino_all_city_data_list, loaded_city_data);
        uint32_t data_arr_index = (uint32_t)old_data_count;
        int absent;
        khint_t data_id_map_slot = dominoCityDataIdMap_put(dominoCityDataIdMap, loaded_city_data.data_id, &absent);
        if (absent < 0) {
            domino_all_city_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to map city_data data_id=%u",
                              loaded_city_data.data_id);
            return -1;
        }
        if (absent == 0) {
            domino_all_city_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate city_data data_id=%u", loaded_city_data.data_id);
            return -1;
        }
        kh_val(dominoCityDataIdMap, data_id_map_slot) = data_arr_index;
        out_city->data = &kv_A(domino_all_city_data_list, data_arr_index);
    }
    return 0;
}

STORAGE_DEFINE_SHARDED_WITH_DATA(City, "city", DominoCity, domino_all_city_list, dominoCityIdMap, dominoCityIdMap_put, dominoCityIdMap_resize,
                                 serializeCity, deserializeCity, DOMINO_SOCIAL_CITY_VEC_RESERVE_CAPACITY_MIN, DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA,
                                 DominoCityData, domino_all_city_data_list, dominoCityDataIdMap, dominoCityDataIdMap_resize)

static yyjson_mut_val* serializeCountryData(yyjson_mut_doc* doc, const DominoCountryData* country_data) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", country_data->data_id);
    yyjson_mut_obj_add_val(doc, obj, "city_id_list", serdeWriteU32Array(doc, country_data->city_id_list, DOMINO_MAX_CITY_COUNT_PER_COUNTRY));
    serdeWriteNameDescription(doc, obj, country_data->name_id, country_data->description_id);
    yyjson_mut_obj_add_val(doc, obj, "interactive_country_id_list",
                           serdeWriteU32Array(doc, country_data->interactive_country_id_list, DOMINO_MAX_INTERACTIVE_COUNTRY_COUNT_PER_COUNTRY));
    yyjson_mut_obj_add_val(doc, obj, "interactive_country_type_list",
                           serdeWriteU8Array(doc, country_data->interactive_country_type_list, DOMINO_MAX_INTERACTIVE_COUNTRY_COUNT_PER_COUNTRY));
    return obj;
}

static int deserializeCountryData(yyjson_val* obj, DominoCountryData* out_country_data) {
    memset(out_country_data, 0, sizeof(DominoCountryData));
    out_country_data->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    serdeReadU32Array(yyjson_obj_get(obj, "city_id_list"), out_country_data->city_id_list, DOMINO_MAX_CITY_COUNT_PER_COUNTRY);
    if (serdeReadNameDescription(obj, &out_country_data->name_id, &out_country_data->description_id) != CODE_OK) {
        return -1;
    }
    serdeReadU32Array(yyjson_obj_get(obj, "interactive_country_id_list"), out_country_data->interactive_country_id_list,
                      DOMINO_MAX_INTERACTIVE_COUNTRY_COUNT_PER_COUNTRY);
    serdeReadU8Array(yyjson_obj_get(obj, "interactive_country_type_list"), out_country_data->interactive_country_type_list,
                     DOMINO_MAX_INTERACTIVE_COUNTRY_COUNT_PER_COUNTRY);
    return 0;
}

static yyjson_mut_val* serializeCountry(yyjson_mut_doc* doc, const DominoCountry* country) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", country->id);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", country->data_id);
    yyjson_mut_obj_add_sint(doc, obj, "wealth", country->wealth);
    yyjson_mut_obj_add_uint(doc, obj, "outer_host_country_id", country->outer_host_country_id);
    yyjson_mut_obj_add_val(doc, obj, "main_leader_id_list",
                           serdeWriteU32Array(doc, country->main_leader_id_list, DOMINO_MAX_MAIN_LEADER_COUNT_PER_COUNTRY));
    yyjson_mut_obj_add_uint(doc, obj, "human_count", country->human_count);
    yyjson_mut_obj_add_uint(doc, obj, "org_count", country->org_count);
    yyjson_mut_obj_add_uint(doc, obj, "building_count", country->building_count);
    yyjson_mut_obj_add_uint(doc, obj, "status", country->status);
    yyjson_mut_obj_add_uint(doc, obj, "type", country->type);
    yyjson_mut_obj_add_uint(doc, obj, "city_count", country->city_count);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", country->region_index);
    yyjson_mut_obj_add_uint(doc, obj, "main_leader_count", country->main_leader_count);

    if (!country->data && country->data_id != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "country id=%u data_id=%u but data is null", country->id,
                          country->data_id);
        return nullptr;
    }
    if (country->data && country->data_id == 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "country id=%u data is not null but data_id is 0", country->id);
        return nullptr;
    }
    if (country->data) {
        DominoCountryData* data_from_id = dominoGetCountryDataByID(country->data_id);
        if (!data_from_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "country id=%u data_id=%u not found", country->id,
                              country->data_id);
            return nullptr;
        }
        if (data_from_id != country->data) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "country id=%u data_id=%u data pointer mismatch", country->id,
                              country->data_id);
            return nullptr;
        }
        yyjson_mut_obj_add_val(doc, obj, "data", serializeCountryData(doc, country->data));
    } else {
        yyjson_mut_obj_add_null(doc, obj, "data");
    }
    return obj;
}

static int deserializeCountry(yyjson_val* obj, DominoCountry* out_country) {
    memset(out_country, 0, sizeof(DominoCountry));
    uint32_t kind_u = 0;
    out_country->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_country->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    out_country->wealth = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "wealth"));
    out_country->outer_host_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "outer_host_country_id"));
    serdeReadU32Array(yyjson_obj_get(obj, "main_leader_id_list"), out_country->main_leader_id_list, DOMINO_MAX_MAIN_LEADER_COUNT_PER_COUNTRY);
    out_country->human_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "human_count"));
    out_country->org_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "org_count"));
    out_country->building_count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "building_count"));
    out_country->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    kind_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    if (kind_u > 255U) {
        return -1;
    }
    out_country->type = (uint8_t)kind_u;
    out_country->city_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "city_count"));
    out_country->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_country->main_leader_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "main_leader_count"));

    yyjson_val* data_val = yyjson_obj_get(obj, "data");
    if (data_val && !yyjson_is_null(data_val)) {
        DominoCountryData loaded_country_data = {0};
        if (deserializeCountryData(data_val, &loaded_country_data) != 0) {
            return -1;
        }
        if (out_country->data_id != loaded_country_data.data_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "country id=%u entity data_id=%u != nested data data_id=%u",
                              out_country->id, out_country->data_id, loaded_country_data.data_id);
            return -1;
        }
        size_t old_data_count = kv_size(domino_all_country_data_list);
        kv_push(DominoCountryData, domino_all_country_data_list, loaded_country_data);
        uint32_t data_arr_index = (uint32_t)old_data_count;
        int absent;
        khint_t data_id_map_slot = dominoCountryDataIdMap_put(dominoCountryDataIdMap, loaded_country_data.data_id, &absent);
        if (absent < 0) {
            domino_all_country_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to map country_data data_id=%u",
                              loaded_country_data.data_id);
            return -1;
        }
        if (absent == 0) {
            domino_all_country_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate country_data data_id=%u",
                              loaded_country_data.data_id);
            return -1;
        }
        kh_val(dominoCountryDataIdMap, data_id_map_slot) = data_arr_index;
        out_country->data = &kv_A(domino_all_country_data_list, data_arr_index);
    }
    return 0;
}

STORAGE_DEFINE_SHARDED_WITH_DATA(Country, "country", DominoCountry, domino_all_country_list, dominoCountryIdMap, dominoCountryIdMap_put,
                                 dominoCountryIdMap_resize, serializeCountry, deserializeCountry, DOMINO_SOCIAL_COUNTRY_VEC_RESERVE_CAPACITY_MIN,
                                 DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA, DominoCountryData, domino_all_country_data_list, dominoCountryDataIdMap,
                                 dominoCountryDataIdMap_resize)

static yyjson_mut_val* serializeAsset(yyjson_mut_doc* doc, const DominoAsset* asset) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "id", asset->id);
    yyjson_mut_obj_add_uint(doc, obj, "parent_id", asset->parent_id);
    yyjson_mut_obj_add_uint(doc, obj, "production_org_id", asset->production_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "sales_org_id", asset->sales_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "dealer_org_id", asset->dealer_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "owner_org_id", asset->owner_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "production_country_id", asset->production_country_id);

    yyjson_mut_obj_add_sint(doc, obj, "value", asset->value);
    yyjson_mut_obj_add_sint(doc, obj, "cost_value", asset->cost_value);
    yyjson_mut_obj_add_sint(doc, obj, "sales_value", asset->sales_value);
    yyjson_mut_obj_add_sint(doc, obj, "dealer_value", asset->dealer_value);
    yyjson_mut_obj_add_sint(doc, obj, "trade_value", asset->trade_value);

    domino_movable_object_id_t movable_object_id = asset->movable_object_id;
    if (movable_object_id != 0) {
        DominoMovableObject* movable_object = dominoGetMovableObjectByID(movable_object_id);
        if (!movable_object) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "asset id=%u movable_object_id=%u not found", asset->id,
                              movable_object_id);
            return nullptr;
        }
        if (movable_object != asset->movable_object) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                              "asset id=%u movable_object_id=%u pointer mismatch (dominoGetMovableObjectByID vs asset->movable_object)", asset->id,
                              movable_object_id);
            return nullptr;
        }
    }
    yyjson_mut_obj_add_uint(doc, obj, "movable_object_id", movable_object_id);
    if (movable_object_id == 0) {
        yyjson_mut_obj_add_sint(doc, obj, "position_x", asset->position_info.x);
        yyjson_mut_obj_add_sint(doc, obj, "position_y", asset->position_info.y);
        yyjson_mut_obj_add_sint(doc, obj, "position_z", asset->position_info.z);
    }

    yyjson_mut_obj_add_uint(doc, obj, "production_time", asset->production_time);
    yyjson_mut_obj_add_uint(doc, obj, "expiration_time", asset->expiration_time);
    yyjson_mut_obj_add_uint(doc, obj, "sales_time", asset->sales_time);
    yyjson_mut_obj_add_uint(doc, obj, "dealer_time", asset->dealer_time);
    yyjson_mut_obj_add_uint(doc, obj, "trade_time", asset->trade_time);

    yyjson_mut_obj_add_uint(doc, obj, "country_id", asset->country_id);
    yyjson_mut_obj_add_uint(doc, obj, "island_id", asset->island_id);
    yyjson_mut_obj_add_uint(doc, obj, "city_id", asset->city_id);
    yyjson_mut_obj_add_uint(doc, obj, "building_id", asset->building_id);
    yyjson_mut_obj_add_uint(doc, obj, "address_id", asset->address_id);
    yyjson_mut_obj_add_uint(doc, obj, "outer_host_country_id", asset->outer_host_country_id);

    yyjson_mut_obj_add_uint(doc, obj, "expiration_months", asset->expiration_months);
    yyjson_mut_obj_add_uint(doc, obj, "count", asset->count);
    yyjson_mut_obj_add_uint(doc, obj, "idle", asset->idle);
    yyjson_mut_obj_add_uint(doc, obj, "status", asset->status);
    yyjson_mut_obj_add_uint(doc, obj, "type", asset->type);
    yyjson_mut_obj_add_uint(doc, obj, "owner_type", asset->owner_type);
    yyjson_mut_obj_add_uint(doc, obj, "space_id", asset->space_id);
    yyjson_mut_obj_add_uint(doc, obj, "space_area_id", asset->space_area_id);
    yyjson_mut_obj_add_uint(doc, obj, "subtype", asset->subtype);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", asset->region_index);
    yyjson_mut_obj_add_uint(doc, obj, "durability", asset->durability);
    yyjson_mut_obj_add_uint(doc, obj, "weight", asset->weight);
    yyjson_mut_obj_add_uint(doc, obj, "volume", asset->volume);
    yyjson_mut_obj_add_uint(doc, obj, "quality", asset->quality);

    return obj;
}

static int deserializeAsset(yyjson_val* obj, DominoAsset* out_asset) {
    memset(out_asset, 0, sizeof(DominoAsset));
    uint32_t kind_u = 0;
    out_asset->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_asset->parent_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "parent_id"));
    out_asset->production_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "production_org_id"));
    out_asset->sales_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "sales_org_id"));
    out_asset->dealer_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "dealer_org_id"));
    out_asset->owner_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "owner_org_id"));
    out_asset->production_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "production_country_id"));

    out_asset->value = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "value"));
    out_asset->cost_value = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "cost_value"));
    out_asset->sales_value = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "sales_value"));
    out_asset->dealer_value = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "dealer_value"));
    out_asset->trade_value = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "trade_value"));

    domino_movable_object_id_t movable_object_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "movable_object_id"));
    out_asset->movable_object_id = movable_object_id;
    if (movable_object_id == 0) {
        out_asset->position_info.x = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_x"));
        out_asset->position_info.y = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_y"));
        out_asset->position_info.z = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_z"));
    } else {
        out_asset->movable_object = nullptr;
    }

    out_asset->production_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "production_time"));
    out_asset->expiration_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "expiration_time"));
    out_asset->sales_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "sales_time"));
    out_asset->dealer_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "dealer_time"));
    out_asset->trade_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "trade_time"));

    out_asset->country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "country_id"));
    out_asset->island_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "island_id"));
    out_asset->city_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "city_id"));
    out_asset->building_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "building_id"));
    out_asset->address_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "address_id"));
    out_asset->outer_host_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "outer_host_country_id"));

    out_asset->expiration_months = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "expiration_months"));
    out_asset->count = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "count"));
    out_asset->idle = (uint16_t)yyjson_get_uint(yyjson_obj_get(obj, "idle"));
    out_asset->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    kind_u = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "type"));
    if (kind_u > 255U) {
        return -1;
    }
    out_asset->type = (uint8_t)kind_u;
    out_asset->owner_type = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "owner_type"));
    out_asset->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_asset->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));
    out_asset->subtype = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "subtype"));
    out_asset->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_asset->durability = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "durability"));
    out_asset->weight = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "weight"));
    out_asset->volume = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "volume"));
    out_asset->quality = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "quality"));
    return 0;
}

STORAGE_DEFINE_SHARDED_SIMPLE(Asset, "asset", DominoAsset, domino_all_asset_list, dominoAssetIdMap, dominoAssetIdMap_put, dominoAssetIdMap_resize,
                              serializeAsset, deserializeAsset, DOMINO_SOCIAL_ASSET_VEC_RESERVE_CAPACITY_MIN, DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA)

DOMINO_CODE serdeValidateAssetReferences(void) {
    for (size_t asset_index = 0; asset_index < kv_size(domino_all_asset_list); asset_index++) {
        const DominoAsset* asset = &kv_A(domino_all_asset_list, asset_index);
        if (asset->movable_object_id == 0) {
            continue;
        }
        if (!dominoGetMovableObjectByID(asset->movable_object_id)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "asset id=%u movable_object_id=%u not found", asset->id,
                              asset->movable_object_id);
            return ERR_INVALID_DATA;
        }
    }
    return CODE_OK;
}

void serdeBindAssetReferences(void) {
    for (size_t asset_index = 0; asset_index < kv_size(domino_all_asset_list); asset_index++) {
        DominoAsset* asset = &kv_A(domino_all_asset_list, asset_index);
        if (asset->movable_object_id != 0) {
            asset->movable_object = dominoGetMovableObjectByID(asset->movable_object_id);
        }
    }
}
