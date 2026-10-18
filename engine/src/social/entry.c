#include "entry.h"

#include "../logger/entry.h"

DominoOrgVec domino_all_org_list;
DominoBuildingVec domino_all_building_list;
DominoAssetVec domino_all_asset_list;
DominoCountryVec domino_all_country_list;
DominoCityVec domino_all_city_list;

DominoOrgDataVec domino_all_org_data_list;
DominoBuildingDataVec domino_all_building_data_list;
DominoCityDataVec domino_all_city_data_list;
DominoCountryDataVec domino_all_country_data_list;

DominoBuildingIDMap* dominoBuildingIdMap;

DominoAssetIDMap* dominoAssetIdMap;
DominoCountryIDMap* dominoCountryIdMap;
DominoCityIDMap* dominoCityIdMap;
DominoOrgIDMap* dominoOrgIdMap;

DominoOrgDataIDMap* dominoOrgDataIdMap;
DominoBuildingDataIDMap* dominoBuildingDataIdMap;
DominoCityDataIDMap* dominoCityDataIdMap;
DominoCountryDataIDMap* dominoCountryDataIdMap;

DominoAsset* dominoGetAssetByID(domino_asset_id_t asset_id) {
    khint_t key = dominoAssetIdMap_get(dominoAssetIdMap, asset_id);
    if (kh_exist(dominoAssetIdMap, key)) {
        return &kv_A(domino_all_asset_list, kh_val(dominoAssetIdMap, key));
    }
    return nullptr;
}

DominoOrg* dominoGetOrgByID(domino_org_id_t org_id) {
    khint_t key = dominoOrgIdMap_get(dominoOrgIdMap, org_id);
    if (kh_exist(dominoOrgIdMap, key)) {
        return &kv_A(domino_all_org_list, kh_val(dominoOrgIdMap, key));
    }
    return nullptr;
}

DominoBuilding* dominoGetBuildingByID(domino_building_id_t building_id) {
    khint_t key = dominoBuildingIdMap_get(dominoBuildingIdMap, building_id);
    if (kh_exist(dominoBuildingIdMap, key)) {
        return &kv_A(domino_all_building_list, kh_val(dominoBuildingIdMap, key));
    }
    return nullptr;
}

DominoCity* dominoGetCityByID(domino_city_id_t city_id) {
    khint_t key = dominoCityIdMap_get(dominoCityIdMap, city_id);
    if (kh_exist(dominoCityIdMap, key)) {
        return &kv_A(domino_all_city_list, kh_val(dominoCityIdMap, key));
    }
    return nullptr;
}

DominoCountry* dominoGetCountryByID(domino_country_id_t country_id) {
    khint_t key = dominoCountryIdMap_get(dominoCountryIdMap, country_id);
    if (kh_exist(dominoCountryIdMap, key)) {
        return &kv_A(domino_all_country_list, kh_val(dominoCountryIdMap, key));
    }
    return nullptr;
}

DominoOrgData* dominoGetOrgDataByID(domino_data_id_t data_id) {
    khint_t key = dominoOrgDataIdMap_get(dominoOrgDataIdMap, data_id);
    if (!kh_exist(dominoOrgDataIdMap, key)) {
        return nullptr;
    }
    return &kv_A(domino_all_org_data_list, kh_val(dominoOrgDataIdMap, key));
}

DominoBuildingData* dominoGetBuildingDataByID(domino_data_id_t data_id) {
    khint_t key = dominoBuildingDataIdMap_get(dominoBuildingDataIdMap, data_id);
    if (!kh_exist(dominoBuildingDataIdMap, key)) {
        return nullptr;
    }
    return &kv_A(domino_all_building_data_list, kh_val(dominoBuildingDataIdMap, key));
}

DominoCityData* dominoGetCityDataByID(domino_data_id_t data_id) {
    khint_t key = dominoCityDataIdMap_get(dominoCityDataIdMap, data_id);
    if (!kh_exist(dominoCityDataIdMap, key)) {
        return nullptr;
    }
    return &kv_A(domino_all_city_data_list, kh_val(dominoCityDataIdMap, key));
}

DominoCountryData* dominoGetCountryDataByID(domino_data_id_t data_id) {
    khint_t key = dominoCountryDataIdMap_get(dominoCountryDataIdMap, data_id);
    if (!kh_exist(dominoCountryDataIdMap, key)) {
        return nullptr;
    }
    return &kv_A(domino_all_country_data_list, kh_val(dominoCountryDataIdMap, key));
}

DOMINO_CODE dominoSocialModuleInit(void) {
    kv_init(domino_all_org_list);
    kv_init(domino_all_building_list);
    kv_init(domino_all_asset_list);
    kv_init(domino_all_country_list);
    kv_init(domino_all_city_list);

    kv_init(domino_all_org_data_list);
    kv_init(domino_all_building_data_list);
    kv_init(domino_all_city_data_list);
    kv_init(domino_all_country_data_list);

    dominoOrgIdMap = dominoOrgIdMap_init();
    if (!dominoOrgIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoOrgIdMap (code=%d)", ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }
    dominoCountryIdMap = dominoCountryIdMap_init();
    if (!dominoCountryIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoCountryIdMap (code=%d)",
                          ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }
    dominoCityIdMap = dominoCityIdMap_init();
    if (!dominoCityIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoCityIdMap (code=%d)", ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }
    dominoBuildingIdMap = dominoBuildingIdMap_init();
    if (!dominoBuildingIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoBuildingIdMap (code=%d)",
                          ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }

    dominoAssetIdMap = dominoAssetIdMap_init();
    if (!dominoAssetIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoAssetIdMap (code=%d)", ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }

    dominoOrgDataIdMap = dominoOrgDataIdMap_init();
    if (!dominoOrgDataIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoOrgDataIdMap (code=%d)",
                          ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }
    dominoBuildingDataIdMap = dominoBuildingDataIdMap_init();
    if (!dominoBuildingDataIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoBuildingDataIdMap (code=%d)",
                          ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }
    dominoCityDataIdMap = dominoCityDataIdMap_init();
    if (!dominoCityDataIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoCityDataIdMap (code=%d)",
                          ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }
    dominoCountryDataIdMap = dominoCountryDataIdMap_init();
    if (!dominoCountryDataIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoCountryDataIdMap (code=%d)",
                          ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }

    return CODE_OK;
}

void dominoSocialModuleExit(void) {
    dominoAssetIdMap_destroy(dominoAssetIdMap);

    dominoBuildingIdMap_destroy(dominoBuildingIdMap);
    dominoCityIdMap_destroy(dominoCityIdMap);
    dominoCountryIdMap_destroy(dominoCountryIdMap);
    dominoOrgIdMap_destroy(dominoOrgIdMap);

    dominoCountryDataIdMap_destroy(dominoCountryDataIdMap);
    dominoCityDataIdMap_destroy(dominoCityDataIdMap);
    dominoBuildingDataIdMap_destroy(dominoBuildingDataIdMap);
    dominoOrgDataIdMap_destroy(dominoOrgDataIdMap);

    kv_destroy(domino_all_country_data_list);
    kv_destroy(domino_all_city_data_list);
    kv_destroy(domino_all_building_data_list);
    kv_destroy(domino_all_org_data_list);

    kv_destroy(domino_all_asset_list);

    kv_destroy(domino_all_building_list);
    kv_destroy(domino_all_city_list);
    kv_destroy(domino_all_country_list);
    kv_destroy(domino_all_org_list);
}
