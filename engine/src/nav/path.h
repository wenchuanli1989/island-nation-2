#ifndef DOMINO_ENGINE_NAV_PATH_H
#define DOMINO_ENGINE_NAV_PATH_H

#include "domino_shared_error_codes.h"
#include "types.h"

/** @file path.h
 * @brief 路径缓存构建与路线寻路。可恢复结果只返回给调用方，Nav 内部不打印；缓存或图数据损坏致命退出。
 */

/** @brief 重新构建一个路网分组的 Floyd 路径缓存。
 * @param out_stat 可为空。
 * @return 分组不存在返回 ERR_NOT_FOUND。
 */
DOMINO_CODE dominoNavBuildPathCache(uint8_t region_index, uint8_t road_network_type, DominoNavFloydStats* out_stat);

/** @brief 为路线寻路：按需构建缓存，确定起终点路口（指定 ID 或离坐标最近的路口），写入中间路口和路径权重。
 * @note 已有 current_fork_road_id 时从它出发；否则有 current_location 时从最近路口出发；都没有时按 start/target 坐标初始化整条路线。
 * @return 端点或分组不存在、路径不可达 ERR_NOT_FOUND；端点已删除 ERR_GAME_STATE_INVALID；参数或分组不符 ERR_INVALID_PARAM；
 *         中间路口超过 DOMINO_MAX_FORK_ROAD_COUNT_PER_ROAD_LINE 时 ERR_BUFFER_TOO_SMALL。
 */
DOMINO_CODE dominoNavRouteRoadLine(DominoRoadLine* road_line);

#endif
