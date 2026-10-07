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

void dominoSocialModuleInit(void) {
    kv_init(domino_all_org_list);
    kv_init(domino_all_building_list);
    kv_init(domino_all_asset_list);
    kv_init(domino_all_country_list);
    kv_init(domino_all_city_list);

    kv_init(domino_all_org_data_list);
    kv_init(domino_all_building_data_list);
    kv_init(domino_all_city_data_list);
    kv_init(domino_all_country_data_list);

    kv_resize(DominoOrg, domino_all_org_list, DOMINO_SOCIAL_ORG_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoOrgData, domino_all_org_data_list, DOMINO_SOCIAL_ORG_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoBuilding, domino_all_building_list, DOMINO_SOCIAL_BUILDING_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoBuildingData, domino_all_building_data_list, DOMINO_SOCIAL_BUILDING_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoAsset, domino_all_asset_list, DOMINO_SOCIAL_ASSET_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoCity, domino_all_city_list, DOMINO_SOCIAL_CITY_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoCityData, domino_all_city_data_list, DOMINO_SOCIAL_CITY_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoCountry, domino_all_country_list, DOMINO_SOCIAL_COUNTRY_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoCountryData, domino_all_country_data_list, DOMINO_SOCIAL_COUNTRY_VEC_RESERVE_CAPACITY_MIN);

    dominoOrgIdMap = dominoOrgIdMap_init();
    dominoCountryIdMap = dominoCountryIdMap_init();
    dominoCityIdMap = dominoCityIdMap_init();
    dominoBuildingIdMap = dominoBuildingIdMap_init();

    dominoAssetIdMap = dominoAssetIdMap_init();

    dominoOrgDataIdMap = dominoOrgDataIdMap_init();
    dominoBuildingDataIdMap = dominoBuildingDataIdMap_init();
    dominoCityDataIdMap = dominoCityDataIdMap_init();
    dominoCountryDataIdMap = dominoCountryDataIdMap_init();

    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_INFO, "socialModuleInit");
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
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_SOCIAL, DOMINO_LOG_LEVEL_INFO, "socialModuleExit");
}
