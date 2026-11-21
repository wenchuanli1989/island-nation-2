#include "entry.h"

#include "domino_shared_common.h"

DominoOrgVec g_domino_org_list;
DominoBuildingVec g_domino_building_list;
DominoAssetVec g_domino_asset_list;
DominoCountryVec g_domino_country_list;
DominoCityVec g_domino_city_list;

DominoOrgDataVec g_domino_org_data_list;
DominoBuildingDataVec g_domino_building_data_list;
DominoCityDataVec g_domino_city_data_list;
DominoCountryDataVec g_domino_country_data_list;

DominoBuildingIdMap* g_domino_building_id_map;

DominoAssetIdMap* g_domino_asset_id_map;
DominoCountryIdMap* g_domino_country_id_map;
DominoCityIdMap* g_domino_city_id_map;
DominoOrgIdMap* g_domino_org_id_map;

DominoOrgDataIdMap* g_domino_org_data_id_map;
DominoBuildingDataIdMap* g_domino_building_data_id_map;
DominoCityDataIdMap* g_domino_city_data_id_map;
DominoCountryDataIdMap* g_domino_country_data_id_map;

DominoAsset* dominoGetAssetById(domino_asset_id_t asset_id) {
    khint_t key = dominoAssetIdMap_get(g_domino_asset_id_map, asset_id);
    if (kh_exist(g_domino_asset_id_map, key)) {
        return &kv_A(g_domino_asset_list, kh_val(g_domino_asset_id_map, key));
    }
    return nullptr;
}

DominoOrg* dominoGetOrgById(domino_org_id_t org_id) {
    khint_t key = dominoOrgIdMap_get(g_domino_org_id_map, org_id);
    if (kh_exist(g_domino_org_id_map, key)) {
        return &kv_A(g_domino_org_list, kh_val(g_domino_org_id_map, key));
    }
    return nullptr;
}

DominoBuilding* dominoGetBuildingById(domino_building_id_t building_id) {
    khint_t key = dominoBuildingIdMap_get(g_domino_building_id_map, building_id);
    if (kh_exist(g_domino_building_id_map, key)) {
        return &kv_A(g_domino_building_list, kh_val(g_domino_building_id_map, key));
    }
    return nullptr;
}

DominoCity* dominoGetCityById(domino_city_id_t city_id) {
    khint_t key = dominoCityIdMap_get(g_domino_city_id_map, city_id);
    if (kh_exist(g_domino_city_id_map, key)) {
        return &kv_A(g_domino_city_list, kh_val(g_domino_city_id_map, key));
    }
    return nullptr;
}

DominoCountry* dominoGetCountryById(domino_country_id_t country_id) {
    khint_t key = dominoCountryIdMap_get(g_domino_country_id_map, country_id);
    if (kh_exist(g_domino_country_id_map, key)) {
        return &kv_A(g_domino_country_list, kh_val(g_domino_country_id_map, key));
    }
    return nullptr;
}

DominoOrgData* dominoGetOrgDataById(domino_data_id_t data_id) {
    khint_t key = dominoOrgDataIdMap_get(g_domino_org_data_id_map, data_id);
    if (!kh_exist(g_domino_org_data_id_map, key)) {
        return nullptr;
    }
    return &kv_A(g_domino_org_data_list, kh_val(g_domino_org_data_id_map, key));
}

DominoBuildingData* dominoGetBuildingDataById(domino_data_id_t data_id) {
    khint_t key = dominoBuildingDataIdMap_get(g_domino_building_data_id_map, data_id);
    if (!kh_exist(g_domino_building_data_id_map, key)) {
        return nullptr;
    }
    return &kv_A(g_domino_building_data_list, kh_val(g_domino_building_data_id_map, key));
}

DominoCityData* dominoGetCityDataById(domino_data_id_t data_id) {
    khint_t key = dominoCityDataIdMap_get(g_domino_city_data_id_map, data_id);
    if (!kh_exist(g_domino_city_data_id_map, key)) {
        return nullptr;
    }
    return &kv_A(g_domino_city_data_list, kh_val(g_domino_city_data_id_map, key));
}

DominoCountryData* dominoGetCountryDataById(domino_data_id_t data_id) {
    khint_t key = dominoCountryDataIdMap_get(g_domino_country_data_id_map, data_id);
    if (!kh_exist(g_domino_country_data_id_map, key)) {
        return nullptr;
    }
    return &kv_A(g_domino_country_data_list, kh_val(g_domino_country_data_id_map, key));
}

void dominoSocialModuleInit(void) {
    g_domino_org_id_map = dominoOrgIdMap_init();
    if (!g_domino_org_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate org ID map");
    }
    g_domino_country_id_map = dominoCountryIdMap_init();
    if (!g_domino_country_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate country ID map");
    }
    g_domino_city_id_map = dominoCityIdMap_init();
    if (!g_domino_city_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate city ID map");
    }
    g_domino_building_id_map = dominoBuildingIdMap_init();
    if (!g_domino_building_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate building ID map");
    }

    g_domino_asset_id_map = dominoAssetIdMap_init();
    if (!g_domino_asset_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate asset ID map");
    }

    g_domino_org_data_id_map = dominoOrgDataIdMap_init();
    if (!g_domino_org_data_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate org data ID map");
    }
    g_domino_building_data_id_map = dominoBuildingDataIdMap_init();
    if (!g_domino_building_data_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate building data ID map");
    }
    g_domino_city_data_id_map = dominoCityDataIdMap_init();
    if (!g_domino_city_data_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate city data ID map");
    }
    g_domino_country_data_id_map = dominoCountryDataIdMap_init();
    if (!g_domino_country_data_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate country data ID map");
    }
}

void dominoSocialModuleDestroy(void) {
    dominoAssetIdMap_destroy(g_domino_asset_id_map);
    g_domino_asset_id_map = nullptr;

    dominoBuildingIdMap_destroy(g_domino_building_id_map);
    g_domino_building_id_map = nullptr;
    dominoCityIdMap_destroy(g_domino_city_id_map);
    g_domino_city_id_map = nullptr;
    dominoCountryIdMap_destroy(g_domino_country_id_map);
    g_domino_country_id_map = nullptr;
    dominoOrgIdMap_destroy(g_domino_org_id_map);
    g_domino_org_id_map = nullptr;

    dominoCountryDataIdMap_destroy(g_domino_country_data_id_map);
    g_domino_country_data_id_map = nullptr;
    dominoCityDataIdMap_destroy(g_domino_city_data_id_map);
    g_domino_city_data_id_map = nullptr;
    dominoBuildingDataIdMap_destroy(g_domino_building_data_id_map);
    g_domino_building_data_id_map = nullptr;
    dominoOrgDataIdMap_destroy(g_domino_org_data_id_map);
    g_domino_org_data_id_map = nullptr;

    kv_destroy(g_domino_country_data_list);
    kv_init(g_domino_country_data_list);
    kv_destroy(g_domino_city_data_list);
    kv_init(g_domino_city_data_list);
    kv_destroy(g_domino_building_data_list);
    kv_init(g_domino_building_data_list);
    kv_destroy(g_domino_org_data_list);
    kv_init(g_domino_org_data_list);

    kv_destroy(g_domino_asset_list);
    kv_init(g_domino_asset_list);

    kv_destroy(g_domino_building_list);
    kv_init(g_domino_building_list);
    kv_destroy(g_domino_city_list);
    kv_init(g_domino_city_list);
    kv_destroy(g_domino_country_list);
    kv_init(g_domino_country_list);
    kv_destroy(g_domino_org_list);
    kv_init(g_domino_org_list);
}
