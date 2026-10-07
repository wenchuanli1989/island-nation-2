#ifndef DOMINO_ENGINE_NAV_PATH_H
#define DOMINO_ENGINE_NAV_PATH_H

#include "domino_shared_error_codes.h"
#include "model_view.h"

/** @brief 构建指定分组的 Floyd 缓存，成功后替换；失败保留旧缓存。out_stat 可为空。 */
DOMINO_CODE dominoNavPathPlanning(uint8_t region_index, uint8_t road_network_type, FloydPathPlanningResult* out_stat);

/** @brief 按需构建缓存，写入 road line 的起终点、中间节点和路径权重。 */
DOMINO_CODE dominoNavigation(DominoRoadLine* road_line);

#endif
