#ifndef DOMINO_ENGINE_NAV_PATH_H
#define DOMINO_ENGINE_NAV_PATH_H

#include "domino_shared_error_codes.h"
#include "model_view.h"

/**
 * @brief 构建指定分组的 Floyd 缓存，成功后替换；失败保留旧缓存。out_stat 可为空。
 * @return 分组缺失返回 ERR_NOT_FOUND；超出网络容量返回 ERR_CAPACITY_EXCEEDED；图不一致返回 ERR_INVALID_DATA。
 */
DOMINO_CODE dominoNavPathPlanning(uint8_t region_index, uint8_t road_network_type, FloydPathPlanningResult* out_stat);

/**
 * @brief 按需构建缓存，写入 road line 的起终点、中间节点和路径权重。
 * @return 分组或端点缺失、路径不可达返回 ERR_NOT_FOUND；路径中间节点超过输出容量返回 ERR_BUFFER_TOO_SMALL。
 */
DOMINO_CODE dominoNavigation(DominoRoadLine* road_line);

#endif
