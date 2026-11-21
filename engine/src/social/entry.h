#ifndef DOMINO_ENGINE_SOCIAL_MODULE_H
#define DOMINO_ENGINE_SOCIAL_MODULE_H

#include "domino_shared_social.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @file entry.h
 * @brief 社会系统：组织、建筑、城市、国家、资产及其扩展 data 的扁平容器和 ID 映射表。
 */

/** @brief 创建 ID 映射表；容器由首次静态初值或上次 Destroy 保持为空。 */
extern void dominoSocialModuleInit(void);

/** @brief 释放全部容器和映射表并复位为空。 */
extern void dominoSocialModuleDestroy(void);

/** @brief 按 ID 查找实体或扩展 data，不存在返回空指针。 */
extern DominoAsset* dominoGetAssetById(domino_asset_id_t asset_id);
extern DominoOrg* dominoGetOrgById(domino_org_id_t org_id);
extern DominoBuilding* dominoGetBuildingById(domino_building_id_t building_id);
extern DominoCity* dominoGetCityById(domino_city_id_t city_id);
extern DominoCountry* dominoGetCountryById(domino_country_id_t country_id);

extern DominoOrgData* dominoGetOrgDataById(domino_data_id_t data_id);
extern DominoBuildingData* dominoGetBuildingDataById(domino_data_id_t data_id);
extern DominoCityData* dominoGetCityDataById(domino_data_id_t data_id);
extern DominoCountryData* dominoGetCountryDataById(domino_data_id_t data_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoOrgIdMap, dominoOrgIdMap, domino_org_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoCountryIdMap, dominoCountryIdMap, domino_country_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoCityIdMap, dominoCityIdMap, domino_city_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoBuildingIdMap, dominoBuildingIdMap, domino_building_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

KHASHL_MAP_INIT(KH_LOCAL, DominoAssetIdMap, dominoAssetIdMap, domino_asset_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoOrgDataIdMap, dominoOrgDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoBuildingDataIdMap, dominoBuildingDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoCityDataIdMap, dominoCityDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoCountryDataIdMap, dominoCountryDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(DominoOrg) DominoOrgVec;
typedef kvec_t(DominoBuilding) DominoBuildingVec;
typedef kvec_t(DominoAsset) DominoAssetVec;
typedef kvec_t(DominoCountry) DominoCountryVec;
typedef kvec_t(DominoCity) DominoCityVec;

typedef kvec_t(DominoOrgData) DominoOrgDataVec;
typedef kvec_t(DominoBuildingData) DominoBuildingDataVec;
typedef kvec_t(DominoCityData) DominoCityDataVec;
typedef kvec_t(DominoCountryData) DominoCountryDataVec;

extern DominoOrgVec g_domino_org_list;
extern DominoBuildingVec g_domino_building_list;
extern DominoAssetVec g_domino_asset_list;
extern DominoCountryVec g_domino_country_list;
extern DominoCityVec g_domino_city_list;

extern DominoOrgDataVec g_domino_org_data_list;
extern DominoBuildingDataVec g_domino_building_data_list;
extern DominoCityDataVec g_domino_city_data_list;
extern DominoCountryDataVec g_domino_country_data_list;

extern DominoOrgIdMap* g_domino_org_id_map;
extern DominoCountryIdMap* g_domino_country_id_map;
extern DominoCityIdMap* g_domino_city_id_map;
extern DominoBuildingIdMap* g_domino_building_id_map;

extern DominoAssetIdMap* g_domino_asset_id_map;

extern DominoOrgDataIdMap* g_domino_org_data_id_map;
extern DominoBuildingDataIdMap* g_domino_building_data_id_map;
extern DominoCityDataIdMap* g_domino_city_data_id_map;
extern DominoCountryDataIdMap* g_domino_country_data_id_map;

#endif
