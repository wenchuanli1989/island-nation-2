#ifndef DOMINO_ENGINE_HOST_MODULE_H
#define DOMINO_ENGINE_HOST_MODULE_H

#include "domino_shared_host.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @file entry.h
 * @brief 宿主世界：地域、岛屿、运动物体和全局 ID 计数器。
 */

/** @brief 创建 ID 映射表；世界状态由首次静态初值或上次 Destroy 保持为空。 */
extern void dominoHostModuleInit(void);

/** @brief 释放容器和映射表，清零全局 ID 计数器与地域状态。 */
extern void dominoHostModuleDestroy(void);

/** @brief 按 ID 查找岛屿或运动物体，不存在返回空指针。 */
extern DominoIsland* dominoGetIslandById(domino_island_id_t island_id);

extern DominoMovableObject* dominoGetMovableObjectById(domino_movable_object_id_t movable_object_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoIslandIdMap, dominoIslandIdMap, domino_island_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoMovableObjectIdMap, dominoMovableObjectIdMap, domino_movable_object_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

extern DominoIdCounters g_domino_id_counters;
/** @brief 固定容量地域数组，前 g_domino_region_count 项有效。 */
extern DominoRegion g_domino_region_list[DOMINO_MAX_REGION_COUNT];
extern uint32_t g_domino_region_count;

typedef kvec_t(DominoIsland) DominoIslandVec;
typedef kvec_t(DominoMovableObject) DominoMovableObjectVec;

extern DominoIslandVec g_domino_island_list;
extern DominoMovableObjectVec g_domino_movable_object_list;
extern DominoIslandIdMap* g_domino_island_id_map;
extern DominoMovableObjectIdMap* g_domino_movable_object_id_map;

#endif
