#ifndef DOMINO_SHARED_SOCIAL_H
#define DOMINO_SHARED_SOCIAL_H

#include <stddef.h>
#include <stdint.h>

#include "domino_shared_host.h"

/**
 * @file domino_shared_social.h
 * @brief 社会系统实体：组织、建筑、城市、国家、资产及其扩展 data。
 */

#define DOMINO_MAX_BUILDING_COUNT_PER_CITY 128

#define DOMINO_MAX_INTERACTIVE_COUNTRY_COUNT_PER_COUNTRY 16
#define DOMINO_MAX_CITY_COUNT_PER_COUNTRY 128

#define DOMINO_MAX_MEMBER_COUNT_PER_ORG 300
#define DOMINO_MAX_INLINE_MEMBER_COUNT_PER_ORG 10
#define DOMINO_MAX_KEY_MEMBER_COUNT_PER_ORG 4  ///< 组织主要成员（高管、董事，不含 boss）上限。
#define DOMINO_MAX_ASSET_COUNT_PER_ORG 128
#define DOMINO_MAX_MAIN_LEADER_COUNT_PER_COUNTRY 8

#define DOMINO_MAX_MEMBER_COUNT_PER_BUILDING 300
#define DOMINO_MAX_INLINE_MEMBER_COUNT_PER_BUILDING 10

#define DOMINO_MAX_ORG_COUNT_PER_CITY 128
#define DOMINO_MAX_MOVING_HUMAN_COUNT_PER_CITY 380
#define DOMINO_MAX_MOVING_ASSET_COUNT_PER_CITY 380
typedef struct {
    domino_data_id_t data_id;
    domino_name_id_t name_id;
    domino_description_id_t description_id;
    domino_human_id_t member_id_list[DOMINO_MAX_MEMBER_COUNT_PER_BUILDING];
    uint8_t member_type_list[DOMINO_MAX_MEMBER_COUNT_PER_BUILDING];

    uint8_t other_info[2584];  ///< 预留，不持久化。
} DominoBuildingData;

static_assert(sizeof(DominoBuildingData) == 4096, "DominoBuildingData must be exactly 4096 bytes");
typedef struct DominoBuilding {
    DominoBuildingData* data;

    union {
        domino_org_id_t owner_org_id;
        domino_human_id_t owner_human_id;
    };

    domino_data_id_t data_id;
    domino_building_id_t id;
    domino_island_id_t island_id;
    domino_city_id_t city_id;
    domino_country_id_t country_id;
    domino_wealth_value_t wealth;

    domino_human_id_t inline_member_id_list[DOMINO_MAX_INLINE_MEMBER_COUNT_PER_BUILDING];  ///< 内联成员（不含拥有者），更多成员放在 data。

    int32_t position_x;
    int32_t position_y;
    int32_t position_z;

    uint32_t outer_host_country_id;  ///< 在外部宿主世界中的国家 ID。

    domino_fork_road_id_t fork_road_id;

    uint16_t member_count;  ///< 成员总数，不含拥有者。

    domino_status_t status;
    domino_type_t type;
    uint8_t region_index;
    uint8_t owner_type;  ///< 决定 owner union 中的 ID 类型。

    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;

    uint8_t inline_member_type_list[DOMINO_MAX_INLINE_MEMBER_COUNT_PER_BUILDING];

    uint8_t other_info[14];  ///< 预留，不持久化。

} DominoBuilding;

static_assert(sizeof(DominoBuilding) == 128, "DominoBuilding must be exactly 128 bytes");

typedef struct DominoCityData {
    domino_data_id_t data_id;
    domino_name_id_t name_id;
    domino_description_id_t description_id;

    domino_org_id_t org_id_list[DOMINO_MAX_ORG_COUNT_PER_CITY];

    domino_building_id_t building_id_list[DOMINO_MAX_BUILDING_COUNT_PER_CITY];

    domino_human_id_t moving_human_id_list[DOMINO_MAX_MOVING_HUMAN_COUNT_PER_CITY];

    domino_asset_id_t moving_asset_id_list[DOMINO_MAX_MOVING_ASSET_COUNT_PER_CITY];

    uint8_t other_info[20];  ///< 预留，不持久化。
} DominoCityData;

static_assert(sizeof(DominoCityData) == 4096, "DominoCityData must be exactly 4096 bytes");

typedef struct DominoCity {
    DominoCityData* data;
    domino_data_id_t data_id;
    domino_org_id_t manager_org_id;

    domino_city_id_t id;
    domino_country_id_t country_id;
    domino_island_id_t island_id;

    domino_wealth_value_t wealth;

    int32_t position_x;
    int32_t position_y;
    int32_t position_z;

    uint32_t outer_host_country_id;  ///< 在外部宿主世界中的国家 ID。

    uint16_t human_count;
    uint16_t org_count;
    uint16_t building_count;
    uint16_t moving_human_count;
    uint16_t moving_asset_count;

    domino_status_t status;
    domino_type_t type;
    uint8_t region_index;

    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;

    uint8_t other_info[65];  ///< 预留，不持久化。

} DominoCity;

static_assert(sizeof(DominoCity) == 128, "DominoCity must be exactly 128 bytes");

typedef struct DominoOrgData {
    domino_data_id_t data_id;
    domino_human_id_t member_id_list[DOMINO_MAX_MEMBER_COUNT_PER_ORG];
    domino_asset_id_t own_asset_id_list[DOMINO_MAX_ASSET_COUNT_PER_ORG];

    domino_name_id_t name_id;
    domino_description_id_t description_id;

    uint8_t other_info[2372];  ///< 预留，不持久化。
} DominoOrgData;

static_assert(sizeof(DominoOrgData) == 4096, "DominoOrgData must be exactly 4096 bytes");

/** @brief 小型组织可仅使用内联成员数组，data_id 为 0 时无扩展数据。 */
typedef struct DominoOrg {
    DominoOrgData* data;
    domino_data_id_t data_id;
    domino_human_id_t boss_id;

    domino_org_id_t id;
    domino_island_id_t island_id;
    domino_city_id_t city_id;
    domino_country_id_t country_id;
    domino_building_id_t building_id;
    domino_wealth_value_t wealth;

    domino_human_id_t founder_id;
    domino_human_id_t key_member_id_list[DOMINO_MAX_KEY_MEMBER_COUNT_PER_ORG];        ///< 主要成员（高管、董事），不含 boss。
    domino_human_id_t inline_member_id_list[DOMINO_MAX_INLINE_MEMBER_COUNT_PER_ORG];  ///< 内联成员，不含主要成员和 boss。

    domino_fork_road_id_t fork_road_id;

    uint32_t outer_host_country_id;  ///< 在外部宿主世界中的国家 ID。

    uint16_t member_count;  ///< 成员总数，不含 boss。

    domino_status_t status;
    domino_type_t type;

    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;
    uint8_t region_index;
    uint8_t own_asset_count;

    uint8_t other_info[12];  ///< 预留，不持久化。

} DominoOrg;

static_assert(sizeof(DominoOrg) == 128, "DominoOrg must be exactly 128 bytes");

typedef struct DominoCountryData {
    domino_data_id_t data_id;
    domino_city_id_t city_id_list[DOMINO_MAX_CITY_COUNT_PER_COUNTRY];
    domino_name_id_t name_id;
    domino_description_id_t description_id;

    domino_country_id_t interactive_country_id_list[DOMINO_MAX_INTERACTIVE_COUNTRY_COUNT_PER_COUNTRY];
    uint8_t interactive_country_type_list[DOMINO_MAX_INTERACTIVE_COUNTRY_COUNT_PER_COUNTRY];

    uint8_t other_info[3492];  ///< 预留，不持久化。

} DominoCountryData;
static_assert(sizeof(DominoCountryData) == 4096, "DominoCountryData must be exactly 4096 bytes");

typedef struct DominoCountry {
    DominoCountryData* data;
    domino_data_id_t data_id;
    domino_country_id_t id;
    domino_wealth_value_t wealth;

    uint32_t outer_host_country_id;  ///< 在外部宿主世界中的国家 ID。

    domino_human_id_t main_leader_id_list[DOMINO_MAX_MAIN_LEADER_COUNT_PER_COUNTRY];

    uint16_t human_count;
    uint16_t org_count;
    uint16_t building_count;

    domino_status_t status;
    domino_type_t type;

    uint8_t city_count;
    uint8_t region_index;

    uint8_t main_leader_count;

    uint8_t other_info[61];  ///< 预留，不持久化。

} DominoCountry;

static_assert(sizeof(DominoCountry) == 128, "DominoCountry must be exactly 128 bytes");

/** @brief 资产记录；movable_object_id 为 0 时 union 保存静态坐标，否则保存绑定后的运动物体指针。 */
typedef struct DominoAsset {
    domino_asset_id_t id;
    domino_asset_id_t parent_id;
    domino_org_id_t production_org_id;
    domino_org_id_t sales_org_id;
    domino_org_id_t dealer_org_id;
    union {
        domino_org_id_t owner_org_id;
        domino_human_id_t owner_human_id;
    };
    domino_country_id_t production_country_id;

    domino_wealth_value_t value;
    domino_wealth_value_t cost_value;
    domino_wealth_value_t sales_value;
    domino_wealth_value_t dealer_value;
    domino_wealth_value_t trade_value;

    union {
        DominoMovableObject* movable_object;

        struct {
            int32_t x;
            int32_t y;
            int32_t z;
        } position;
    };

    domino_movable_object_id_t movable_object_id;  ///< 运动物体 ID，0 表示无。

    /* 各环节发生的业务时刻。 */
    domino_runtime_ms_t production_at_ms;
    domino_runtime_ms_t expiration_at_ms;
    domino_runtime_ms_t sales_at_ms;
    domino_runtime_ms_t dealer_at_ms;
    domino_runtime_ms_t trade_at_ms;

    domino_country_id_t country_id;
    domino_island_id_t island_id;
    domino_city_id_t city_id;
    domino_building_id_t building_id;
    domino_fork_road_id_t fork_road_id;

    uint32_t outer_host_country_id;  ///< 在外部宿主世界中的国家 ID。

    uint16_t count;
    uint16_t idle;

    domino_status_t status;
    domino_type_t type;

    uint8_t expiration_months;  ///< 保质期，单位月。
    uint8_t owner_type;         ///< 决定 owner union 中的 ID 类型。

    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;

    uint8_t subtype;
    uint8_t region_index;

    uint8_t durability;
    uint8_t weight;
    uint8_t volume;
    uint8_t quality;

} DominoAsset;

static_assert(sizeof(DominoAsset) == 128, "DominoAsset must be exactly 128 bytes");

#endif
