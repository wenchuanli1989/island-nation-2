#ifndef DOMINO_ENGINE_HUMAN_MODULE_H
#define DOMINO_ENGINE_HUMAN_MODULE_H

#include "domino_shared_human.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

#define HUMAN_VEC_RESERVE_CAPACITY_MIN 1024u

#define DOMINO_BEHAVIOR_TASK_VEC_RESERVE_CAPACITY_MIN 1024u
#define DOMINO_BEHAVIOR_VEC_RESERVE_CAPACITY_MIN 1024u

extern void dominoHumanModuleInit(void);

extern void dominoHumanModuleExit(void);

extern DominoHuman* dominoGetHumanByID(domino_human_id_t human_id);

extern DominoHumanData* dominoGetHumanDataByID(domino_data_id_t data_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoHumanIDMap, dominoHumanIdMap, domino_human_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoHumanDataIDMap, dominoHumanDataIdMap, domino_data_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(DominoHuman) DominoHumanVec;
typedef kvec_t(DominoHumanData) DominoHumanDataVec;

extern DominoHumanVec domino_all_human_list;
extern DominoHumanDataVec domino_all_human_data_list;
extern DominoHumanIDMap* dominoHumanIdMap;
extern DominoHumanDataIDMap* dominoHumanDataIdMap;

extern DominoTask* dominoGetTaskByID(domino_task_id_t task_id);

/** @brief 按 (task_id, sequence_index) 查找行为；序号从 0 开始，未找到返回 nullptr。 */
extern DominoBehavior* dominoGetBehaviorByTaskIDAndSequenceIndex(domino_task_id_t task_id, uint8_t sequence_index);

static inline uint64_t dominoMakeTaskBehaviorKey(domino_task_id_t task_id, uint8_t sequence_index) {
    return ((uint64_t)task_id << 8U) | (uint64_t)sequence_index;
}

KHASHL_MAP_INIT(KH_LOCAL, DominoTaskIDMap, dominoTaskIdMap, domino_task_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, DominoTaskBehaviorKeyMap, dominoTaskBehaviorKeyMap, uint64_t, uint32_t, kh_hash_uint64, kh_eq_generic)

typedef kvec_t(DominoTask) DominoTaskVec;
typedef kvec_t(DominoBehavior) DominoBehaviorVec;

extern DominoTaskVec domino_all_task_list;
extern DominoBehaviorVec domino_all_behavior_list;

extern DominoTaskIDMap* dominoTaskIdMap;
extern DominoTaskBehaviorKeyMap* dominoTaskBehaviorKeyMap;
extern int dominoHumanBehaviorPlanningThread(void* arg_ptr);
#endif
