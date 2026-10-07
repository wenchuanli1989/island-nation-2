#ifndef DOMINO_ENGINE_SOCIAL_MODULE_H
#define DOMINO_ENGINE_SOCIAL_MODULE_H

#include "domino_shared_social.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

#define DOMINO_SOCIAL_ORG_VEC_RESERVE_CAPACITY_MIN 1024u
#define DOMINO_SOCIAL_BUILDING_VEC_RESERVE_CAPACITY_MIN 1024u
#define DOMINO_SOCIAL_ASSET_VEC_RESERVE_CAPACITY_MIN 1024u
#define DOMINO_SOCIAL_CITY_VEC_RESERVE_CAPACITY_MIN 1024u
#define DOMINO_SOCIAL_COUNTRY_VEC_RESERVE_CAPACITY_MIN 1024u

extern void dominoSocialModuleInit(void);

extern void dominoSocialModuleExit(void);

extern DominoAsset* dominoGetAssetByID(domino_asset_id_t asset_id);
extern DominoOrg* dominoGetOrgByID(domino_org_id_t org_id);
extern DominoBuilding* dominoGetBuildingByID(domino_building_id_t building_id);
extern DominoCity* dominoGetCityByID(domino_city_id_t city_id);
extern DominoCountry* dominoGetCountryByID(domino_country_id_t country_id);

extern DominoOrgData* dominoGetOrgDataByID(domino_data_id_t data_id);
extern DominoBuildingData* dominoGetBuildingDataByID(domino_data_id_t data_id);
extern DominoCityData* dominoGetCityDataByID(domino_data_id_t data_id);
extern DominoCountryData* dominoGetCountryDataByID(domino_data_id_t data_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoOrgIDMap, dominoOrgIdMap, domino_org_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoCountryIDMap, dominoCountryIdMap, domino_country_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoCityIDMap, dominoCityIdMap, domino_city_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoBuildingIDMap, dominoBuildingIdMap, domino_building_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

KHASHL_MAP_INIT(KH_LOCAL, DominoAssetIDMap, dominoAssetIdMap, domino_asset_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoOrgDataIDMap, dominoOrgDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoBuildingDataIDMap, dominoBuildingDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoCityDataIDMap, dominoCityDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoCountryDataIDMap, dominoCountryDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(DominoOrg) DominoOrgVec;
typedef kvec_t(DominoBuilding) DominoBuildingVec;
typedef kvec_t(DominoAsset) DominoAssetVec;
typedef kvec_t(DominoCountry) DominoCountryVec;
typedef kvec_t(DominoCity) DominoCityVec;

typedef kvec_t(DominoOrgData) DominoOrgDataVec;
typedef kvec_t(DominoBuildingData) DominoBuildingDataVec;
typedef kvec_t(DominoCityData) DominoCityDataVec;
typedef kvec_t(DominoCountryData) DominoCountryDataVec;

extern DominoOrgVec domino_all_org_list;
extern DominoBuildingVec domino_all_building_list;
extern DominoAssetVec domino_all_asset_list;
extern DominoCountryVec domino_all_country_list;
extern DominoCityVec domino_all_city_list;

extern DominoOrgDataVec domino_all_org_data_list;
extern DominoBuildingDataVec domino_all_building_data_list;
extern DominoCityDataVec domino_all_city_data_list;
extern DominoCountryDataVec domino_all_country_data_list;

extern DominoOrgIDMap* dominoOrgIdMap;
extern DominoCountryIDMap* dominoCountryIdMap;
extern DominoCityIDMap* dominoCityIdMap;
extern DominoBuildingIDMap* dominoBuildingIdMap;

extern DominoAssetIDMap* dominoAssetIdMap;

extern DominoOrgDataIDMap* dominoOrgDataIdMap;
extern DominoBuildingDataIDMap* dominoBuildingDataIdMap;
extern DominoCityDataIDMap* dominoCityDataIdMap;
extern DominoCountryDataIDMap* dominoCountryDataIdMap;

#endif
