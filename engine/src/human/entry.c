#include "entry.h"

#include "../logger/entry.h"
#include "store.h"

DominoHumanVec domino_all_human_list;
DominoHumanDataVec domino_all_human_data_list;
DominoHumanIDMap* dominoHumanIdMap;

DominoHuman* dominoGetHumanByID(domino_human_id_t human_id) {
    khint_t key = dominoHumanIdMap_get(dominoHumanIdMap, human_id);
    if (kh_exist(dominoHumanIdMap, key)) {
        return &kv_A(domino_all_human_list, kh_val(dominoHumanIdMap, key));
    }
    return nullptr;
}

DominoHumanData* dominoGetHumanDataByID(domino_human_id_t human_id) {
    khint_t key = dominoHumanIdMap_get(dominoHumanIdMap, human_id);
    if (!kh_exist(dominoHumanIdMap, key)) {
        return nullptr;
    }
    return &kv_A(domino_all_human_data_list, kh_val(dominoHumanIdMap, key));
}

/** @brief 释放人物容器；允许初始化失败后的部分资源。 */
static void releaseHumanEntities(void) {
    dominoHumanIdMap_destroy(dominoHumanIdMap);
    dominoHumanIdMap = nullptr;
    kv_destroy(domino_all_human_list);
    kv_destroy(domino_all_human_data_list);
    kv_init(domino_all_human_list);
    kv_init(domino_all_human_data_list);
}

DOMINO_CODE dominoHumanModuleInit(void) {
    kv_init(domino_all_human_list);
    kv_init(domino_all_human_data_list);
    kv_resize(DominoHuman, domino_all_human_list, HUMAN_VEC_RESERVE_CAPACITY_MIN);
    kv_resize(DominoHumanData, domino_all_human_data_list, HUMAN_VEC_RESERVE_CAPACITY_MIN);
    dominoHumanIdMap = dominoHumanIdMap_init();
    if (!dominoHumanIdMap) {
        releaseHumanEntities();
        return ERR_MEMORY_ALLOC;
    }

    DOMINO_CODE result = dominoHumanStoreInit();
    if (result != CODE_OK) {
        releaseHumanEntities();
        return result;
    }

    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_INFO, "humanModuleInit");
    return CODE_OK;
}

void dominoHumanModuleExit(void) {
    dominoHumanStoreExit();
    releaseHumanEntities();
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_INFO, "humanModuleExit");
}
