#ifndef DOMINO_ENGINE_NAV_STATE_H
#define DOMINO_ENGINE_NAV_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "model_view.h"
#include "storage_view.h"

/** @brief 初始化实体、分段、ID map 和路径缓存状态；实体数组沿用 kvec，分配失败时终止进程。 */
DOMINO_CODE navStateInit(void);

/** @brief 释放全部 Nav 状态；先释放借用实体地址的缓存，再释放实体与分段容器，也支持初始化失败时清理。 */
void navStateDestroy(void);

bool navStateIsReady(void);

/** @brief 实体增删后置 dirty；保存前完成排序与分段重建后清除。 */
bool navStateNeedsCompaction(void);

void navStateMarkDirty(void);

void navStateMarkCanonical(void);

/**
 * @brief 校验 fork road/road line 的 ID 并输出最大 ID；道路端点由随后执行的邻接重建统一校验。
 * @note Nav 已初始化，Storage 已建立唯一 ID map，navPersistenceValidateSegments 已校验 fork road/road 数量和分段容量。
 *       两个输出指针非空；失败时不改写输出。
 */
DOMINO_CODE navStateValidateLoadedData(domino_fork_road_id_t* out_max_fork_id, domino_road_line_id_t* out_max_road_line_id);

/** @brief 全部加载校验和邻接重建成功后校准下一 ID；两个最大 ID 已通过校验，均小于 UINT32_MAX。 */
void navStateAdvanceNextIds(domino_fork_road_id_t max_fork_id, domino_road_line_id_t max_road_line_id);

/**
 * @brief 保存前只读校验实体、ID 映射和邻接关系，不一致时返回 ERR_INVALID_DATA。
 * @note Nav 已初始化；加载校验和新增入口保证实体数量不超过 DOMINO_NAV_ID_MAP_ENTITY_COUNT_MAX。
 */
DOMINO_CODE navStateValidateRuntimeData(void);

/** @brief 刷新 fork road 与 road ID map 的数组下标；仅在保存前图校验完成、实体 canonical 排序后调用。 */
void navStateRefreshIdMapIndices(void);

/** @brief 以下查找返回借用指针；缺失时返回 nullptr，数组扩容或排序后需重新按 ID 查找。 */
DominoForkRoad* dominoGetForkRoadByID(domino_fork_road_id_t fork_road_id);

DominoRoad* dominoGetRoadByID(domino_road_id_t road_id);

DominoRoadLine* dominoGetRoadLineByID(domino_road_line_id_t road_line_id);

/** @brief 按地域和路网类型查找分段下标，未找到时返回 -1；调用方保证 segment_list 非空。 */
int navStateFindSegmentIndex(const DominoNavDataSegmentVec* segment_list, uint8_t region_index, uint8_t network_type);

/** @brief 取得分段借用指针；segment_list 须非空，缺失时返回 nullptr，分段数组扩容或重建后失效。 */
const DominoNavDataSegment* navStateFindSegment(const DominoNavDataSegmentVec* segment_list, uint8_t region_index, uint8_t network_type);

/** @brief 释放全部路径矩阵和实体指针表，保留缓存槽供后续复用。 */
void navStateInvalidateAllPathCaches(void);

/** @brief 释放指定路网的路径矩阵和实体指针表。 */
void navStateInvalidatePathCache(uint8_t region_index, uint8_t road_network_type);

/**
 * @brief 取得指定路网的完整路径缓存借用指针。
 * @return 缓存完整可用时返回指针，缺失或已失效时返回 nullptr。
 * @note 缓存失效、替换或 Nav 状态销毁后须重新获取；调用方不得释放缓存或其数组。
 */
[[nodiscard]] const DominoNavPathCache* navStateGetPathCache(uint8_t region_index, uint8_t road_network_type);

/** @brief 取得或分配指定路网缓存槽；分配失败返回 nullptr，已有缓存内容保持不变。 */
DominoNavPathCache* navStatePreparePathCache(uint8_t region_index, uint8_t road_network_type);

/**
 * @brief 替换已准备的缓存槽并接管两个数组，无失败提交。
 * @note cache 来自 navStatePreparePathCache；两个数组非空且与旧缓存不重叠，vertex_count 在有效路网上限内且非零。
 *       调用方已完成所有可能返回错误的步骤；实体数组和分段在准备、规划、提交期间不得发生并发修改。
 */
void navStateCommitPathCache(DominoNavPathCache* cache, PathInfo* path_matrix, DominoForkRoad** fork_road_list,
                             domino_nav_vertex_index_t vertex_count);

#endif
