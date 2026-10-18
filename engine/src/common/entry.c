#include "entry.h"

#include "../logger/entry.h"

DominoNameVec domino_all_name_list;
DominoDescriptionVec domino_all_description_list;

DominoNameIDMap* dominoNameIdMap;
DominoDescriptionIDMap* dominoDescriptionIdMap;

DOMINO_CODE dominoCommonModuleInit(void) {
    kv_init(domino_all_name_list);
    kv_init(domino_all_description_list);
    dominoNameIdMap = dominoNameIdMap_init();
    if (!dominoNameIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_COMMON, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoNameIdMap (code=%d)", ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }
    dominoDescriptionIdMap = dominoDescriptionIdMap_init();
    if (!dominoDescriptionIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_COMMON, DOMINO_LOG_LEVEL_ERROR, "failed to allocate dominoDescriptionIdMap (code=%d)",
                          ERR_MEMORY_ALLOC);
        return ERR_MEMORY_ALLOC;
    }
    return CODE_OK;
}

void dominoCommonModuleExit(void) {
    kv_destroy(domino_all_name_list);
    kv_destroy(domino_all_description_list);
    dominoNameIdMap_destroy(dominoNameIdMap);
    dominoDescriptionIdMap_destroy(dominoDescriptionIdMap);
}

domino_name_t* dominoGetNameByID(domino_name_id_t name_id) {
    if (name_id == 0) {
        return nullptr;
    }
    khint_t key = dominoNameIdMap_get(dominoNameIdMap, name_id);
    if (kh_exist(dominoNameIdMap, key)) {
        return &kv_A(domino_all_name_list, kh_val(dominoNameIdMap, key));
    }
    return nullptr;
}

domino_description_t* dominoGetDescriptionByID(domino_description_id_t description_id) {
    if (description_id == 0) {
        return nullptr;
    }
    khint_t key = dominoDescriptionIdMap_get(dominoDescriptionIdMap, description_id);
    if (kh_exist(dominoDescriptionIdMap, key)) {
        return &kv_A(domino_all_description_list, kh_val(dominoDescriptionIdMap, key));
    }
    return nullptr;
}
