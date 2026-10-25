#ifndef DOMINO_ENGINE_SOCIAL_MODULE_H
#define DOMINO_ENGINE_SOCIAL_MODULE_H

#include "domino_shared_social.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @brief 创建社会实体及扩展 data 的 ID map；分配失败直接打印到 stderr 并终止进程。
 * @pre 由引擎串行保证 Init → Destroy 顺序；容器由首次静态初始化或上次 Destroy 保持为空。
 */
extern void dominoSocialModuleInit(void);

/** @brief 释放社会实体及扩展 data 容器和 ID map，立即清空全部容器状态及全局 map 指针。
 * @pre 当前实例已 Init，且引擎已停止线程并完成保存；由生命周期控制方串行调用。
 * @note 完成后可重新 Init；不重复校验引擎已保证的生命周期顺序。
 */
extern void dominoSocialModuleDestroy(void);

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
