#ifndef DOMINO_SHARED_HUMAN_H
#define DOMINO_SHARED_HUMAN_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_host.h"
#include "domino_shared_human_task.h"

/**
 * @file domino_shared_human.h
 * @brief 人类实体的 64 字节核心记录和 4096 字节领域扩展 data 类型。
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

/** @brief 人物业务状态；异常状态持久化，执行器跳过其任务、需求和规划，不自动恢复。 */
typedef enum {
    DOMINO_HUMAN_STATUS_NORMAL = 0,  ///< 正常参与模拟。
    DOMINO_HUMAN_STATUS_EXCEPTION,   ///< 已提交任务失败或取消，保留业务现场并停止后续执行。
    DOMINO_HUMAN_STATUS_COUNT,
} DOMINO_HUMAN_STATUS;

/** @brief 六项需求共用的百分制上限，字段直接保存整数分。 */
enum {
    DOMINO_HUMAN_NEED_MAX = 100U,
};

/** @brief 4096 字节人物数据；与核心共享 human_id 和数组索引，前部集中日程、需求与当前位置。 */
typedef struct DominoHumanData {
    /* 活动任务、决策、需求与生命状态；以下字段共占前 40 字节。 */
    uint64_t random_state;                                                  ///< 人物独立随机状态；规划使用副本，提交后保存后继状态。
    domino_human_id_t human_id;                                             ///< 与配对 DominoHuman.human_id 相同，必须非零。
    domino_task_id_t active_task_id_list[DOMINO_HUMAN_TASK_CHANNEL_COUNT];  ///< 主通道与两个通用次通道的活动任务 ID，0 表示空槽。
    uint32_t schedule_revision;                                             ///< 日程结构版本，结构或安排改变时递增，禁止回绕。
    domino_runtime_ms_t needs_updated_at_ms;  ///< 自然需求已结算到的累计未冻结现实毫秒；按完整分钟推进，未满分钟保留，0 合法。
    uint8_t energy;                           ///< 精力，0..100 整数分，自然变化按现实分钟结算。
    uint8_t food;                             ///< 饱食度，0..100 整数分，自然变化按现实分钟结算。
    uint8_t water;                            ///< 水分充足度，0..100 整数分，自然变化按现实分钟结算。
    uint8_t hygiene;                          ///< 清洁度，0..100 整数分，自然变化按现实分钟结算。
    uint8_t recreation;                       ///< 娱乐满足度，0..100 整数分，自然变化按现实分钟结算。
    uint8_t social;                           ///< 社交满足度，0..100 整数分，自然变化按现实分钟结算。
    uint8_t health;
    uint8_t body_hp_value;

    /* 高频位置、财富与待处理调度参数。 */
    domino_country_id_t current_country_id;
    domino_city_id_t current_city_id;
    domino_building_id_t current_building_id;
    domino_island_id_t current_island_id;
    domino_fork_road_id_t current_address;  // 当前最近的地点，寻路系统使用
    domino_fork_road_id_t sleep_address;    // 休息地址，可以是家庭地址，旅馆酒店地址，公司地址
    domino_wealth_value_t wealth;
    uint32_t pending_plan_reasons;  ///< 运行态：合并的待规划原因位集合，不持久化。

    /* 本人持有的全部任务；按提交顺序追加，删除时前移，状态和计划时间变化不重排。 */
    domino_task_id_t task_id_list[DOMINO_HUMAN_TASK_COUNT_MAX];  ///< 前 task_count 项有效，包含活动、待执行及尚未回收的终态任务。
    uint8_t task_count;                                          ///< 持有任务数量，0..32；数组位置不代表执行顺序。

    /* 其他身体状态、情绪与外观。 */
    uint8_t nutrition;
    uint8_t sleep;
    uint8_t exercise;
    uint8_t emotion;
    uint8_t skin_color;
    uint8_t hair_color;
    uint8_t eye_color;
    uint8_t face_shape;
    uint8_t body_shape;
    uint8_t voice;
    uint8_t height;
    uint8_t weight;

    /* 组织、工作地址与职业属性。 */
    domino_org_id_t company_org_id;
    domino_fork_road_id_t company_address;
    domino_org_id_t org_id_list[DOMINO_MAX_ORG_COUNT_PER_HUMAN];
    domino_human_id_t org_boss_id_list[DOMINO_MAX_ORG_BOSS_COUNT_PER_HUMAN];
    uint8_t job;
    uint8_t influence;
    uint8_t fame;
    uint8_t org_info;  // 是否是雇员，是否是公司老板，是否是反抗组织头目，是否是帮派头目，是否是宗教组织头目，是否是临时性组织头目

    /* 身份与基本人口属性。 */
    domino_name_id_t name_id;
    domino_description_id_t description_id;
    domino_country_id_t country_id;
    char id_card[DOMINO_HUMAN_ID_CARD_LENGTH + 1];  // 身份证，0开头代表外来者
    uint8_t generation;                             ///< 人物所属家族的代数。
    uint8_t gender;
    uint8_t age;

    /* 家庭、社交关系及最近接触记录。 */
    domino_org_id_t family_org_id;
    domino_fork_road_id_t family_address;
    domino_human_id_t partner_id;
    domino_human_id_t father_id;
    domino_human_id_t mother_id;
    domino_human_id_t child_id_list[DOMINO_MAX_CHILD_COUNT_PER_FAMILY];
    domino_human_id_t sibling_id_list[DOMINO_MAX_CHILD_COUNT_PER_FAMILY - 1];
    domino_human_id_t friend_id_list[DOMINO_MAX_FRIEND_COUNT_PER_HUMAN];
    domino_human_id_t colleague_id_list[DOMINO_MAX_COLLEAGUE_COUNT_PER_HUMAN];
    domino_human_id_t recent_contact_id_list[DOMINO_MAX_RECENT_CONTACT_HUMAN_COUNT];  // 最近接触人物列表，环形数组
    uint8_t family_info;  // 是否结婚，是否有子女，是否有兄弟姐妹，是否有父母，是否是户主，是否是家庭子女
    uint8_t family_love;
    uint8_t couple_love;
    uint8_t friendship;
    uint8_t recent_contact_list_count;
    uint8_t recent_contact_id_list_head_index;

    /* 资产列表与当前装备。 */
    domino_asset_id_t own_asset_id_list[DOMINO_MAX_ASSET_COUNT_PER_HUMAN];
    domino_asset_id_t hold_asset_id_list[DOMINO_MAX_ASSET_COUNT_HOLD_PER_HUMAN];  // 当前装备资产列表
    uint8_t own_asset_list_count;
    uint8_t hold_asset_list_count;
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

    /* 能力、性格、兴趣与价值观。 */
    uint8_t intelligence;
    uint8_t talent;
    uint8_t character;
    uint8_t hobby;
    uint8_t belief;
    uint8_t world_view;
    uint8_t life_view;
    uint8_t value_view;

    /* 历史位置与最近访问记录。 */
    uint32_t outer_host_country_id;  // 在外宿主世界的国家ID
    uint32_t last_active_time;       ///< 最后活跃时刻，单位为累积模拟秒。
    domino_country_id_t last_active_country_id;
    domino_city_id_t last_active_city_id;
    domino_building_id_t last_active_building_id;
    domino_country_id_t birth_country_id;
    domino_city_id_t birth_city_id;
    domino_building_id_t birth_building_id;
    uint32_t death_time;  ///< 死亡时刻，单位为累积模拟秒。
    domino_country_id_t death_country_id;
    domino_city_id_t death_city_id;
    domino_building_id_t death_building_id;
    domino_fork_road_id_t recent_visited_address_id_list[DOMINO_MAX_RECENT_VISITED_ADDRESS_COUNT];  // 最近去过的地址列表，环形数组
    uint8_t last_active_region_index;
    domino_space_id_t last_active_space_id;
    domino_space_area_id_t last_active_area_id;
    uint8_t birth_region_index;
    domino_space_id_t birth_space_id;
    domino_space_area_id_t birth_area_id;
    uint8_t death_region_index;
    domino_space_id_t death_space_id;
    domino_space_area_id_t death_area_id;
    uint8_t recent_visited_address_list_count;
    uint8_t recent_visited_address_id_list_head_index;

    uint8_t other_info[2529];  ///< 预留补齐 4096 字节，不持久化。
} DominoHumanData;

static_assert(sizeof(DominoHumanData) == 4096, "DominoHumanData must be exactly 4096 bytes");

/** @brief 64 字节人物核心，集中保存身份、空间定位和运行调度标识；data 必须存在且独占。 */
typedef struct DominoHuman {
    DominoHumanData* data;            ///< 借用同索引的 Data；数组布局改变后须重新绑定。
    uint64_t active_plan_request_id;  ///< 运行态：已提交且仍有效的规划请求 ID，0 表示无有效请求，不持久化。
    uint64_t wakeup_game_timer_id;    ///< 运行态：人物唯一有效唤醒计时器 ID，0 表示未登记，不持久化。

    /* movable_object_id 为 0 时保存静态坐标，否则保存绑定后的运动对象指针。 */
    union {
        DominoMovableObject* movable_object;
        struct {
            int32_t x;
            int32_t y;
            int32_t z;
        } position_info;
    };

    domino_runtime_ms_t wakeup_due_ms;             ///< 运行态：有效唤醒的绝对截止点，仅 wakeup_game_timer_id 非零时有效，不持久化。
    domino_human_id_t human_id;                    ///< 核心、Data 和索引统一使用的人物身份，必须非零。
    domino_movable_object_id_t movable_object_id;  ///< 运动对象 ID，0 表示无关联对象。
    domino_status_t status;                        ///< 持久化业务状态，取 DOMINO_HUMAN_STATUS。
    uint8_t region_index;
    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;
    bool plan_submit_pending_flag;  ///< 运行态：有规划请求等待提交，可与有效请求并存，不持久化。
    bool execution_queued_flag;     ///< 运行态：已进入执行器就绪集合，用于防止重复入队，不表示正在执行，不持久化。
    uint8_t other_info[6];          ///< 补齐 64 字节，不持久化。
} DominoHuman;

static_assert(sizeof(DominoHuman) == 64, "DominoHuman must be exactly 64 bytes");

#endif
