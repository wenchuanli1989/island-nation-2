#include "entry.h"

#include "domino_shared_common.h"

DominoNameVec domino_all_name_list;
DominoDescriptionVec domino_all_description_list;

DominoNameIDMap* dominoNameIdMap;
DominoDescriptionIDMap* dominoDescriptionIdMap;

void dominoCommonModuleInit(void) {
    dominoNameIdMap = dominoNameIdMap_init();
    if (!dominoNameIdMap) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate dominoNameIdMap");
    }
    dominoDescriptionIdMap = dominoDescriptionIdMap_init();
    if (!dominoDescriptionIdMap) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate dominoDescriptionIdMap");
    }
}

void dominoCommonModuleDestroy(void) {
    kv_destroy(domino_all_name_list);
    kv_init(domino_all_name_list);
    kv_destroy(domino_all_description_list);
    kv_init(domino_all_description_list);
    dominoNameIdMap_destroy(dominoNameIdMap);
    dominoNameIdMap = nullptr;
    dominoDescriptionIdMap_destroy(dominoDescriptionIdMap);
    dominoDescriptionIdMap = nullptr;
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
