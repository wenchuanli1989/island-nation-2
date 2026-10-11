#ifndef DOMINO_ENGINE_HUMAN_MODULE_H
#define DOMINO_ENGINE_HUMAN_MODULE_H

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

#define HUMAN_VEC_RESERVE_CAPACITY_MIN 1024u

extern DOMINO_CODE dominoHumanModuleInit(void);

extern void dominoHumanModuleExit(void);

extern DominoHuman* dominoGetHumanByID(domino_human_id_t human_id);

/** @brief Human 与 Data 共用 human_id 索引；加载期间可在 data 指针绑定前查询。 */
extern DominoHumanData* dominoGetHumanDataByID(domino_human_id_t human_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoHumanIDMap, dominoHumanIdMap, domino_human_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(DominoHuman) DominoHumanVec;
typedef kvec_t(DominoHumanData) DominoHumanDataVec;

/** @brief 两个数组按相同索引一一配对，由 human_id 共用同一张 ID 映射。 */
extern DominoHumanVec domino_all_human_list;
extern DominoHumanDataVec domino_all_human_data_list;
extern DominoHumanIDMap* dominoHumanIdMap;

extern int dominoHumanBehaviorPlanningThread(void* arg_ptr);
#endif
