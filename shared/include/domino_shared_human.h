#ifndef DOMINO_SHARED_HUMAN_H
#define DOMINO_SHARED_HUMAN_H

#include <stdint.h>

#include "domino_shared_behavior.h"
#include "domino_shared_host.h"

/**
 * @file domino_shared_human.h
 * @brief 人类实体的基础 128 字节热数据和 4096 字节扩展 data 类型。
 */

#define DOMINO_MIN_BEAR_AGE 3
#define DOMINO_MAX_BEAR_AGE 18
#define DOMINO_MAX_HUMAN_AGE 24
#define DOMINO_MAX_CHILD_COUNT_PER_FAMILY 5

#define DOMINO_HUMAN_ID_CARD_LENGTH 8  // 身份证长度，前四位祖先代号，后四位个人代号

#define DOMINO_MAX_RECENT_CONTACT_HUMAN_COUNT 64
#define DOMINO_MAX_RECENT_VISITED_ADDRESS_COUNT 64

#define DOMINO_MAX_ASSET_COUNT_PER_HUMAN 128
#define DOMINO_MAX_ASSET_COUNT_HOLD_PER_HUMAN 16  // 当前持有/携带资产数量上限，出入境，一个资产可以是容器包含其他资产

#define DOMINO_MAX_ASSET_COUNT_PER_TRADE 16  // 一次贸易可以包含的资产数量上限，数量合并之后按照商品类型分类统计

#define DOMINO_MAX_FRIEND_COUNT_PER_HUMAN 5
#define DOMINO_MAX_COLLEAGUE_COUNT_PER_HUMAN 5
#define DOMINO_MAX_ORG_COUNT_PER_HUMAN 5
#define DOMINO_MAX_ORG_BOSS_COUNT_PER_HUMAN 5

#define DOMINO_MAX_TASK_COUNT_PER_HUMAN 32

typedef struct DominoHumanData {
    /* 任务字段尚未接入调度和存档；inner_task_list 用于单人模式的内联任务。 */
    domino_task_id_t task_id_list[DOMINO_MAX_TASK_COUNT_PER_HUMAN];

    DominoTask inner_task_list[DOMINO_MAX_TASK_COUNT_PER_HUMAN / 2];

    domino_data_id_t data_id;
    domino_asset_id_t own_asset_id_list[DOMINO_MAX_ASSET_COUNT_PER_HUMAN];
    domino_asset_id_t hold_asset_id_list[DOMINO_MAX_ASSET_COUNT_HOLD_PER_HUMAN];  // 当前装备资产列表

    domino_human_id_t partner_id;
    domino_human_id_t father_id;
    domino_human_id_t mother_id;

    domino_human_id_t child_id_list[DOMINO_MAX_CHILD_COUNT_PER_FAMILY];
    domino_human_id_t sibling_id_list[DOMINO_MAX_CHILD_COUNT_PER_FAMILY - 1];

    domino_org_id_t org_id_list[DOMINO_MAX_ORG_COUNT_PER_HUMAN];

    domino_human_id_t org_boss_id_list[DOMINO_MAX_ORG_BOSS_COUNT_PER_HUMAN];

    domino_human_id_t friend_id_list[DOMINO_MAX_FRIEND_COUNT_PER_HUMAN];

    domino_human_id_t colleague_id_list[DOMINO_MAX_COLLEAGUE_COUNT_PER_HUMAN];

    uint32_t death_time;        ///< 死亡时刻，单位为累积模拟秒。
    uint32_t last_active_time;  ///< 最后活跃时刻，单位为累积模拟秒。

    domino_country_id_t last_active_country_id;
    domino_city_id_t last_active_city_id;
    domino_building_id_t last_active_building_id;

    domino_country_id_t birth_country_id;
    domino_city_id_t birth_city_id;
    domino_building_id_t birth_building_id;
    domino_country_id_t death_country_id;
    domino_city_id_t death_city_id;
    domino_building_id_t death_building_id;

    domino_human_id_t recent_contact_id_list[DOMINO_MAX_RECENT_CONTACT_HUMAN_COUNT];                // 最近接触人物列表，环形数组
    domino_fork_road_id_t recent_visited_address_id_list[DOMINO_MAX_RECENT_VISITED_ADDRESS_COUNT];  // 最近去过的地址列表，环形数组

    uint8_t last_active_region_index;
    uint8_t birth_region_index;
    uint8_t death_region_index;
    uint8_t recent_contact_list_count;
    uint8_t recent_contact_id_list_head_index;
    uint8_t recent_visited_address_list_count;
    uint8_t recent_visited_address_id_list_head_index;

    char id_card[DOMINO_HUMAN_ID_CARD_LENGTH + 1];  // 身份证，0开头代表外来者
    domino_name_id_t name_id;
    domino_description_id_t description_id;

    uint8_t generation;

    domino_space_id_t last_active_space_id;
    domino_space_area_id_t last_active_area_id;

    domino_space_id_t birth_space_id;
    domino_space_area_id_t birth_area_id;
    domino_space_id_t death_space_id;
    domino_space_area_id_t death_area_id;

    uint8_t skin_color;
    uint8_t hair_color;
    uint8_t eye_color;
    uint8_t face_shape;
    uint8_t body_shape;
    uint8_t voice;

    uint8_t hat;
    uint8_t hair;
    uint8_t glasses;
    uint8_t earring;
    uint8_t necklace;
    uint8_t coat;
    uint8_t trouser;
    uint8_t shoe;
    uint8_t sock;
    uint8_t glove;
    uint8_t ring;
    uint8_t bracelet;
    uint8_t belt;
    uint8_t bag;

    uint8_t task_id_list_count;
    uint8_t inner_task_list_count;

    uint8_t active_task_index;

    uint8_t other_info[1306];  ///< 预留，不持久化。
} DominoHumanData;

static_assert(sizeof(DominoHumanData) == 4096, "DominoHumanData must be exactly 4096 bytes");
typedef struct DominoHuman {
    /* data 借用全局扩展数组；movable_object_id 为 0 时 union 保存静态坐标，否则保存绑定后的指针。 */
    DominoHumanData* data;

    union {
        DominoMovableObject* movable_object;

        struct {
            int32_t x;
            int32_t y;
            int32_t z;
        } position_info;
    };

    domino_movable_object_id_t movable_object_id;  // 运动物体ID，为0表示无

    domino_data_id_t data_id;

    domino_org_id_t family_org_id;
    domino_org_id_t company_org_id;

    domino_human_id_t id;
    domino_country_id_t country_id;
    domino_country_id_t current_country_id;
    domino_city_id_t current_city_id;
    domino_building_id_t current_building_id;
    domino_island_id_t current_island_id;

    domino_wealth_value_t wealth;

    domino_fork_road_id_t current_address;  // 当前最近的地点，寻路系统使用
    domino_fork_road_id_t family_address;
    domino_fork_road_id_t company_address;
    domino_fork_road_id_t sleep_address;  // 休息地址，可以是家庭地址，旅馆酒店地址，公司地址

    uint32_t outer_host_country_id;  // 在外宿主世界的国家ID

    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;

    uint8_t behavior;

    uint8_t health;
    uint8_t energy;
    uint8_t food;
    uint8_t water;
    uint8_t emotion;

    uint8_t nutrition;
    uint8_t hygiene;
    uint8_t sleep;
    uint8_t exercise;

    uint8_t recreation;
    uint8_t social;
    uint8_t family_love;
    uint8_t couple_love;
    uint8_t friendship;

    domino_status_t status;

    uint8_t job;
    uint8_t influence;
    uint8_t fame;

    uint8_t body_hp_value;
    uint8_t gender;
    uint8_t age;
    uint8_t height;
    uint8_t weight;

    uint8_t intelligence;
    uint8_t talent;
    uint8_t character;
    uint8_t hobby;

    uint8_t belief;
    uint8_t world_view;
    uint8_t life_view;
    uint8_t value_view;

    uint8_t family_info;  // 是否结婚，是否有子女，是否有兄弟姐妹，是否有父母，是否是户主，是否是家庭子女
    uint8_t org_info;     // 是否是雇员，是否是公司老板，是否是反抗组织头目，是否是帮派头目，是否是宗教组织头目，是否是临时性组织头目

    uint8_t region_index;

    uint8_t hold_asset_list_count;
    uint8_t own_asset_list_count;

    uint8_t other_info[1];  ///< 预留，不持久化。

} DominoHuman;

static_assert(sizeof(DominoHuman) == 128, "DominoHuman must be exactly 128 bytes");

#endif
