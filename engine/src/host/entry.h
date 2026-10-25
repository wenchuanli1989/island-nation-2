#ifndef DOMINO_ENGINE_HOST_MODULE_H
#define DOMINO_ENGINE_HOST_MODULE_H

#include "domino_shared_host.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @brief 创建 host ID map；分配失败直接打印到 stderr 并终止进程。
 * @pre 由引擎串行保证 Init → Destroy 顺序；世界固定状态和容器由首次静态初始化或上次 Destroy 清空。
 */
extern void dominoHostModuleInit(void);

/** @brief 释放并清空 host 容器和 ID map，重置全部全局 ID、地域数量及地域字段；原子成员逐项写零。
 * @pre 当前实例已 Init，且引擎已停止线程并完成保存；由生命周期控制方串行调用。
 * @note 完成后可重新 Init；不重复校验引擎已保证的生命周期顺序。
 */
extern void dominoHostModuleDestroy(void);

extern DominoIsland* dominoGetIslandByID(domino_island_id_t island_id);

extern DominoMovableObject* dominoGetMovableObjectByID(domino_movable_object_id_t movable_object_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoIslandIDMap, dominoIslandIdMap, domino_island_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoMovableObjectIDMap, dominoMovableObjectIdMap, domino_movable_object_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

extern DominoGlobalIncrementID g_domino_global_increment_id;
/** @brief 固定容量地域数组，实际有效数量由 `g_domino_region_count` 决定。 */
extern DominoRegion g_domino_region_list[DOMINO_MAX_REGION_COUNT];
extern uint32_t g_domino_region_count;

typedef kvec_t(DominoIsland) DominoIslandVec;
typedef kvec_t(DominoMovableObject) DominoMovableObjectVec;

extern DominoIslandVec domino_all_island_list;
extern DominoMovableObjectVec domino_all_movable_object_list;
extern DominoIslandIDMap* dominoIslandIdMap;
extern DominoMovableObjectIDMap* dominoMovableObjectIdMap;

#endif
