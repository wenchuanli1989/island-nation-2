#ifndef DOMINO_ENGINE_COMMON_MODULE_H
#define DOMINO_ENGINE_COMMON_MODULE_H

#include <stdint.h>

#include "domino_shared_types.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

extern void dominoCommonModuleInit(void);

extern void dominoCommonModuleExit(void);

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
