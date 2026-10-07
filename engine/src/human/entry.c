#include "entry.h"

#include "../logger/entry.h"

DominoHumanVec domino_all_human_list;
DominoHumanDataVec domino_all_human_data_list;
DominoHumanIDMap* dominoHumanIdMap;
DominoHumanDataIDMap* dominoHumanDataIdMap;

DominoTaskVec domino_all_task_list;
DominoBehaviorVec domino_all_behavior_list;

DominoTaskIDMap* dominoTaskIdMap;
DominoTaskBehaviorKeyMap* dominoTaskBehaviorKeyMap;

DominoHuman* dominoGetHumanByID(domino_human_id_t human_id) {
    khint_t key = dominoHumanIdMap_get(dominoHumanIdMap, human_id);
    if (kh_exist(dominoHumanIdMap, key)) {
        return &kv_A(domino_all_human_list, kh_val(dominoHumanIdMap, key));
    }
    return nullptr;
}

DominoHumanData* dominoGetHumanDataByID(domino_data_id_t data_id) {
    khint_t key = dominoHumanDataIdMap_get(dominoHumanDataIdMap, data_id);
    if (!kh_exist(dominoHumanDataIdMap, key)) {
        return nullptr;
    }
    return &kv_A(domino_all_human_data_list, kh_val(dominoHumanDataIdMap, key));
}

DominoTask* dominoGetTaskByID(domino_task_id_t task_id) {
    khint_t key = dominoTaskIdMap_get(dominoTaskIdMap, task_id);
    if (kh_exist(dominoTaskIdMap, key)) {
        return &kv_A(domino_all_task_list, kh_val(dominoTaskIdMap, key));
    }
    return nullptr;
}

DominoBehavior* dominoGetBehaviorByTaskIDAndSequenceIndex(domino_task_id_t task_id, uint8_t sequence_index) {
    uint64_t behavior_key = dominoMakeTaskBehaviorKey(task_id, sequence_index);
    khint_t map_index = dominoTaskBehaviorKeyMap_get(dominoTaskBehaviorKeyMap, behavior_key);
    if (kh_exist(dominoTaskBehaviorKeyMap, map_index)) {
        return &kv_A(domino_all_behavior_list, kh_val(dominoTaskBehaviorKeyMap, map_index));
    }
    return nullptr;
}

void dominoHumanModuleInit(void) {
    kv_init(domino_all_human_list);
    kv_init(domino_all_human_data_list);
    kv_resize(DominoHuman, domino_all_human_list, HUMAN_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoHumanData, domino_all_human_data_list, HUMAN_VEC_RESERVE_CAPACITY_MIN);
    dominoHumanIdMap = dominoHumanIdMap_init();
    dominoHumanDataIdMap = dominoHumanDataIdMap_init();

    kv_init(domino_all_task_list);
    kv_init(domino_all_behavior_list);

    kv_resize(DominoTask, domino_all_task_list, DOMINO_BEHAVIOR_TASK_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoBehavior, domino_all_behavior_list, DOMINO_BEHAVIOR_VEC_RESERVE_CAPACITY_MIN);

    dominoTaskIdMap = dominoTaskIdMap_init();
    dominoTaskBehaviorKeyMap = dominoTaskBehaviorKeyMap_init();

    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_INFO, "humanModuleInit");
}

void dominoHumanModuleExit(void) {
    dominoHumanIdMap_destroy(dominoHumanIdMap);
    dominoHumanDataIdMap_destroy(dominoHumanDataIdMap);
    kv_destroy(domino_all_human_list);
    kv_destroy(domino_all_human_data_list);

    dominoTaskBehaviorKeyMap_destroy(dominoTaskBehaviorKeyMap);
    dominoTaskIdMap_destroy(dominoTaskIdMap);

    kv_destroy(domino_all_behavior_list);
    kv_destroy(domino_all_task_list);
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_INFO, "humanModuleExit");
}
