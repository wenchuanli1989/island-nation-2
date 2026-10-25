#ifndef DOMINO_SHARED_TYPES_H
#define DOMINO_SHARED_TYPES_H

#include <stdbool.h>
#include <stdint.h>

enum { COMMON_NAME_LENGTH = 64, COMMON_DESCRIPTION_LENGTH = 512 };

typedef char domino_account_id_t[128];
typedef char domino_host_world_id_t[128];
typedef char domino_account_country_id_t[128];

typedef uint8_t domino_type_t;
typedef uint8_t domino_status_t;

typedef uint32_t domino_name_id_t;
typedef uint32_t domino_description_id_t;

typedef char domino_name_t[COMMON_NAME_LENGTH];
typedef char domino_description_t[COMMON_DESCRIPTION_LENGTH];

/**
 * @brief 业务时刻和时长统一使用 32 位累计未冻结现实毫秒；0 和 UINT32_MAX 均为合法值。
 * @note 固定使用 uint32_t，不扩宽为 64 位；加法先检查范围，减法先检查先后关系，不回绕或重置累计时钟。
 *       time 内部的纳秒采样、UTC 墙上时间不改变此业务约定。
 */
typedef uint32_t domino_runtime_ms_t;

/* 实体 ID 在宿主世界内使用；哈希表将 ID 映射到扁平数组下标。 */
typedef uint32_t domino_human_id_t;
typedef uint32_t domino_asset_id_t;  // ID可回绕
typedef uint32_t domino_org_id_t;
typedef uint32_t domino_task_id_t;  // ID可回绕

typedef uint32_t domino_island_id_t;
typedef uint32_t domino_city_id_t;
typedef uint32_t domino_building_id_t;
typedef uint32_t domino_fork_road_id_t;

typedef uint64_t domino_road_id_t;
typedef uint32_t domino_road_line_id_t;
typedef uint32_t domino_country_id_t;

typedef uint32_t domino_movable_object_id_t;

typedef int32_t domino_wealth_value_t;

typedef uint32_t domino_data_id_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
} DominoLocation;

/** @brief 二维坐标；两个分量均为 INT32_MIN 时表示未指定位置。 */
typedef struct {
    int32_t x;
    int32_t y;
} DominoLocationXY;

static inline bool dominoLocationXYIsNull(DominoLocationXY location) {
    return location.x == INT32_MIN && location.y == INT32_MIN;
}
#endif
