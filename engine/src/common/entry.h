#ifndef DOMINO_ENGINE_COMMON_MODULE_H
#define DOMINO_ENGINE_COMMON_MODULE_H

#include <stdint.h>

#include "domino_shared_types.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @brief 创建公共实体 ID map；分配失败直接打印到 stderr 并终止进程。
 * @pre 由引擎串行保证 Init → Destroy 顺序；容器由首次静态初始化或上次 Destroy 保持为空。
 */
extern void dominoCommonModuleInit(void);

/** @brief 释放名称、描述容器和 ID map，立即清空容器指针、数量、容量及全局 map 指针。
 * @pre 当前实例已 Init，且引擎已停止线程并完成保存；由生命周期控制方串行调用。
 * @note 完成后可重新 Init；不重复校验引擎已保证的生命周期顺序。
 */
extern void dominoCommonModuleDestroy(void);

/** @brief 查找名称；ID 为 0 或不存在时返回 nullptr，结果借用全局数组。 */
extern domino_name_t* dominoGetNameByID(domino_name_id_t name_id);

/** @brief 查找描述；ID 为 0 或不存在时返回 nullptr，结果借用全局数组。 */
extern domino_description_t* dominoGetDescriptionByID(domino_description_id_t description_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoNameIDMap, dominoNameIdMap, domino_name_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoDescriptionIDMap, dominoDescriptionIdMap, domino_description_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(domino_name_t) DominoNameVec;
typedef kvec_t(domino_description_t) DominoDescriptionVec;

extern DominoNameVec domino_all_name_list;
extern DominoDescriptionVec domino_all_description_list;
extern DominoNameIDMap* dominoNameIdMap;
extern DominoDescriptionIDMap* dominoDescriptionIdMap;

#endif
