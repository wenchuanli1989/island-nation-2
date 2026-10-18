#ifndef DOMINO_ENGINE_HOST_MODULE_H
#define DOMINO_ENGINE_HOST_MODULE_H

#include "domino_shared_error_codes.h"
#include "domino_shared_host.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @brief 清空世界固定状态并初始化 host 容器和 ID map；失败直接返回，已取得的资源保留到进程终结。 */
extern DOMINO_CODE dominoHostModuleInit(void);

extern void dominoHostModuleExit(void);

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
