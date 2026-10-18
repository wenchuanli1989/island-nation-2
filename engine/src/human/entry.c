#include "entry.h"

#include "../logger/entry.h"
#include "executor.h"
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

/** @brief 正常退出时释放已初始化的人物容器。 */
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
    dominoHumanIdMap = dominoHumanIdMap_init();
    if (!dominoHumanIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot allocate human ID map");
        return ERR_MEMORY_ALLOC;
    }

    DOMINO_CODE result = dominoHumanStoreInit();
    if (result != CODE_OK) {
        return result;
    }
    result = dominoHumanExecutionInit();
    if (result != CODE_OK) {
        return result;
    }

    return CODE_OK;
}

void dominoHumanModuleExit(void) {
    dominoHumanExecutionExit();
    dominoHumanStoreExit();
    releaseHumanEntities();
}
