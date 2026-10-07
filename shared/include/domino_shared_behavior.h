#ifndef DOMINO_SHARED_BEHAVIOR_H
#define DOMINO_SHARED_BEHAVIOR_H

#include <stdint.h>

#include "domino_shared_host.h"
#include "domino_shared_types.h"

/**
 * @file domino_shared_behavior.h
 * @brief 任务与行为步骤的数据结构。
 */

enum {
    DOMINO_MAX_BEHAVIOR_PARTICIPANT_COUNT = 8,
    DOMINO_TASK_SIZE_MAX = 84,
    DOMINO_BEHAVIOR_SIZE_MAX = 112,
};

static_assert(DOMINO_MAX_BEHAVIOR_PARTICIPANT_COUNT <= UINT8_MAX, "participant_count cannot represent the configured limit");

/** @brief 参与者角色位集合，可同时标记记录拥有者、执行者等角色。 */
typedef enum {
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_NONE = 0,
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_OWNER = 1U << 0,
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_ACTOR = 1U << 1,
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_TARGET = 1U << 2,
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_COUNTERPARTY = 1U << 3,
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_WITNESS = 1U << 4,
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_AFFECTED = 1U << 5,
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_REQUIRED = 1U << 6,
    DOMINO_BEHAVIOR_PARTICIPANT_ROLE_OPTIONAL = 1U << 7,
} DOMINO_BEHAVIOR_PARTICIPANT_ROLE;

/** @brief 任务类型；当前行为步骤也复用此枚举。 */
typedef enum {
    DOMINO_TASK_TYPE_NONE = 0,
    DOMINO_TASK_TYPE_WORK = 1,
    DOMINO_TASK_TYPE_SHOPPING = 2,
    DOMINO_TASK_TYPE_ENTERTAINMENT = 3,
    DOMINO_TASK_TYPE_REST = 4,
    DOMINO_TASK_TYPE_VISIT = 5,
    DOMINO_TASK_TYPE_TRAVEL = 6,
    DOMINO_TASK_TYPE_MEDICAL = 7,
    DOMINO_TASK_TYPE_PARADE = 8,
} DOMINO_TASK_TYPE;

/** @brief 任务生命周期状态，写入 `domino_status_t status`。 */
typedef enum {
    DOMINO_TASK_STATUS_NOT_STARTED = 0,
    DOMINO_TASK_STATUS_IN_PROGRESS = 1,
    DOMINO_TASK_STATUS_ENDED = 2,  // 已结束，结果由时间戳和 result_code 区分
    DOMINO_TASK_STATUS_ARCHIVED = 3,
} DOMINO_TASK_STATUS;

/**
 * @brief 任务计划和执行记录；当前尚未实现状态推进。
 * 计划时间与实际执行时间分开保存；结束结果由 finished_time、failed_time 和 result_code 描述。
 */
typedef struct DominoTask {
    DominoLocation location;

    domino_task_id_t task_id;

    domino_building_id_t building_id;
    domino_fork_road_id_t address_id;  // 最近导航地址

    domino_human_id_t owner_human_id;  // 记录拥有者或决策主体
    domino_human_id_t actor_human_id;  // 默认执行者
    domino_human_id_t target_human_id;

    domino_game_date_time_t start_date_time;  // 解析后的绝对计划开始时间
    domino_game_date_time_t end_date_time;    // 解析后的绝对计划结束时间

    domino_game_date_time_t started_time;   // 实际开始执行时间
    domino_game_date_time_t finished_time;  // 正常执行完成时间
    domino_game_date_time_t failed_time;    // 执行失败/中断时间

    domino_game_date_t start_date;  // 计划日期要求，0 表示无要求

    domino_game_time_t earliest_start_time;
    domino_game_time_t start_time;
    domino_game_time_t end_time;
    domino_game_time_t deadline_time;
    domino_game_time_t duration_time;

    uint8_t participant_count;  // 任务级去重参与者数量
    uint8_t behavior_count;
    uint8_t progress_value;
    uint8_t progress_target_value;
    uint8_t current_behavior_sequence_index;  // 当前行为序号，从 0 开始且小于 behavior_count
    uint8_t retry_count;

    domino_status_t status;  // DOMINO_TASK_STATUS
    domino_type_t type;      // 任务总体类型或默认行为类型，DOMINO_TASK_TYPE

    uint8_t execution_mode;
    uint8_t result_code;
    uint8_t priority;
    uint8_t region_index;
    uint8_t precision;  // 地点精度，由调用方约定
} DominoTask;

static_assert(sizeof(DominoTask) <= DOMINO_TASK_SIZE_MAX, "DominoTask exceeds DOMINO_TASK_SIZE_MAX");

/**
 * @brief 任务触发的单个行为步骤。
 *
 * (task_id, sequence_index) 是行为索引键；参与者 ID 与三个属性数组使用相同下标。
 */
typedef struct DominoBehavior {
    DominoLocation location;

    domino_task_id_t task_id;

    domino_country_id_t country_id;
    domino_island_id_t island_id;
    domino_city_id_t city_id;
    domino_building_id_t building_id;
    domino_fork_road_id_t address_id;  // 最近导航地址

    domino_human_id_t participant_human_id_list[DOMINO_MAX_BEHAVIOR_PARTICIPANT_COUNT];

    domino_game_time_t earliest_start_time;
    domino_game_time_t start_time;
    domino_game_time_t end_time;
    domino_game_time_t deadline_time;
    domino_game_time_t duration_time;

    uint8_t participant_role_flags_list[DOMINO_MAX_BEHAVIOR_PARTICIPANT_COUNT];       // DOMINO_BEHAVIOR_PARTICIPANT_ROLE 位集合
    uint8_t participant_response_status_list[DOMINO_MAX_BEHAVIOR_PARTICIPANT_COUNT];  // 邀请/接受/拒绝等响应状态
    uint8_t participant_importance_list[DOMINO_MAX_BEHAVIOR_PARTICIPANT_COUNT];       // 参与重要性或权重，0-255

    domino_space_id_t space_id;
    domino_space_area_id_t space_area_id;
    uint8_t participant_count;  // 有效参与者下标范围为 [0, participant_count)
    uint8_t sequence_index;     // 在所属任务中的触发顺序，从 0 开始且同一任务内唯一
    domino_type_t type;         // 此步骤的行为类型，DOMINO_TASK_TYPE
    uint8_t region_index;
    uint8_t precision;  // 地点精度，由调用方约定
} DominoBehavior;

static_assert(sizeof(DominoBehavior) <= DOMINO_BEHAVIOR_SIZE_MAX, "DominoBehavior exceeds DOMINO_BEHAVIOR_SIZE_MAX");

#endif
