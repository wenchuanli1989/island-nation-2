#ifndef DOMINO_ENGINE_NAV_PATH_H
#define DOMINO_ENGINE_NAV_PATH_H

#include "domino_shared_error_codes.h"
#include "model_view.h"

/**
 * @brief 构建并替换指定分组的 Floyd 缓存；图损坏、容量不一致或分配失败直接报错并终止。out_stat 可为空。
 * @pre Nav 已初始化且无并发销毁；生命周期由引擎保证。
 * @return 分组缺失返回 ERR_NOT_FOUND，否则返回 CODE_OK。
 * @note 可恢复业务结果不在 Nav 内部打印，由调用边界按自身阶段记录。
 */
DOMINO_CODE dominoNavPathPlanning(uint8_t region_index, uint8_t road_network_type, FloydPathPlanningResult* out_stat);

/**
 * @brief 按需构建缓存，写入 road line 的起终点、中间节点和路径权重。
 * @pre Nav 已初始化且无并发销毁；生命周期由引擎保证。
 * @return 分组或端点缺失、路径不可达返回 ERR_NOT_FOUND；路径中间节点超过输出容量返回 ERR_BUFFER_TOO_SMALL。
 * @note 参数错误和失效端点保留业务错误返回；路径缓存损坏直接报错并终止。
 * @note 可恢复业务结果不在 Nav 内部打印，由调用边界按自身阶段记录。
 */
DOMINO_CODE dominoNavigation(DominoRoadLine* road_line);

#endif
