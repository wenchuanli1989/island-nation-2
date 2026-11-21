#ifndef DOMINO_ENGINE_COMMON_MODULE_H
#define DOMINO_ENGINE_COMMON_MODULE_H

#include <stdint.h>

#include "domino_shared_types.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @file entry.h
 * @brief 名称和描述字符串表：ID 映射到扁平数组下标。名称／描述 ID 在加载时重新分配，不跨会话保持。
 */

/** @brief 创建 ID 映射表；容器由首次静态初值或上次 Destroy 保持为空。 */
extern void dominoCommonModuleInit(void);

/** @brief 释放字符串表和映射表并复位为空。 */
extern void dominoCommonModuleDestroy(void);

/** @brief 查找名称，ID 为 0 或不存在返回空指针；结果借用全局数组。 */
extern domino_name_t* dominoGetNameById(domino_name_id_t name_id);

/** @brief 查找描述，约定同 dominoGetNameById。 */
extern domino_description_t* dominoGetDescriptionById(domino_description_id_t description_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoNameIdMap, dominoNameIdMap, domino_name_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoDescriptionIdMap, dominoDescriptionIdMap, domino_description_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(domino_name_t) DominoNameVec;
typedef kvec_t(domino_description_t) DominoDescriptionVec;

extern DominoNameVec g_domino_name_list;
extern DominoDescriptionVec g_domino_description_list;
extern DominoNameIdMap* g_domino_name_id_map;
extern DominoDescriptionIdMap* g_domino_description_id_map;

#endif
