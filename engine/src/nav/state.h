#ifndef DOMINO_ENGINE_NAV_STATE_H
#define DOMINO_ENGINE_NAV_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "types.h"

/** @file state.h
 * @brief Nav 运行期状态：实体容器、分段、ID 映射表、路径缓存和压实标志。
 * @note Nav 的查询与修改由调用方串行协调。数组扩容、排序或缓存替换后，之前取得的借用指针全部失效，须按 ID 重新查询。
 */

extern DominoForkRoadVec g_domino_fork_road_list;
extern DominoRoadVec g_domino_road_list;
extern DominoRoadLineVec g_domino_road_line_list;
extern DominoNavSegmentVec g_domino_fork_road_segment_list;
extern DominoNavSegmentVec g_domino_road_segment_list;

/** @brief 实体 ID 到数组下标的映射。 */
extern DominoForkRoadIdMap* g_domino_fork_road_id_map;
extern DominoRoadIdMap* g_domino_road_id_map;
extern DominoRoadLineIdMap* g_domino_road_line_id_map;

/** @brief 创建 ID 映射表；容器和缓存由首次静态初值或上次 Destroy 保持为空。 */
void navStateInit(void);

/** @brief 先释放借用实体地址的路径缓存，再释放实体、分段和映射表并复位全部状态。 */
void navStateDestroy(void);

/** @brief Nav 资源是否已初始化，供独立公开的道路增删入口检查。 */
bool navStateIsReady(void);

/** @brief 布局是否需要在保存前压实；实体增删后为真，压实为 canonical 布局后为假。 */
bool navStateNeedsCompaction(void);

void navStateMarkDirty(void);

void navStateMarkCanonical(void);

/** @brief 检查加载的实体数和分组路口数在 Nav 上限内，超限致命退出。 */
void navStateValidateLoadedCapacity(void);

/** @brief canonical 排序后刷新 fork road 与 road 映射表中的数组下标。 */
void navStateRefreshIdMapIndices(void);

/** @brief 按 ID 查找路口，输出借用指针。
 * @return 找到返回 CODE_OK；ID 为 0 或不存在返回 ERR_NOT_FOUND 并输出空指针。
 */
DOMINO_CODE navStateFindForkRoadById(domino_fork_road_id_t fork_road_id, DominoForkRoad** out_fork_road);

/** @brief 按 ID 查找道路，输出借用指针；返回值约定同 navStateFindForkRoadById。 */
DOMINO_CODE navStateFindRoadById(domino_road_id_t road_id, DominoRoad** out_road);

/** @brief 按 ID 查找路线，不存在返回空指针。 */
DominoRoadLine* dominoGetRoadLineById(domino_road_line_id_t road_line_id);

/** @brief 按分组键查找分段下标，未找到返回 -1。 */
int navStateFindSegmentIndex(const DominoNavSegmentVec* segment_list, uint8_t region_index, uint8_t network_type);

/** @brief 按分组键取得分段借用指针，未找到返回空指针。 */
const DominoNavSegment* navStateFindSegment(const DominoNavSegmentVec* segment_list, uint8_t region_index, uint8_t network_type);

/** @brief 释放全部路径矩阵和指针表，保留缓存槽供复用。 */
void navStateInvalidateAllPathCaches(void);

void navStateInvalidatePathCache(uint8_t region_index, uint8_t road_network_type);

/** @brief 取得分组的有效路径缓存，缺失或已失效返回空指针；调用方只读。 */
[[nodiscard]] const DominoNavPathCache* navStateGetPathCache(uint8_t region_index, uint8_t road_network_type);

/** @brief 取得或分配分组的缓存槽。 */
DominoNavPathCache* navStatePreparePathCache(uint8_t region_index, uint8_t road_network_type);

/** @brief 用新构建的矩阵和指针表替换缓存槽内容，接管两个数组的所有权。
 * @pre cache 来自 navStatePreparePathCache；vertex_count 非零，两个数组非空且不与旧缓存重叠。
 */
void navStateCommitPathCache(DominoNavPathCache* cache, DominoNavPathCell* path_matrix, DominoForkRoad** fork_road_list,
                             domino_nav_vertex_index_t vertex_count);

#endif
