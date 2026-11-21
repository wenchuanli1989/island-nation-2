#include "entry.h"

#include "domino_shared_common.h"

DominoNameVec g_domino_name_list;
DominoDescriptionVec g_domino_description_list;

DominoNameIdMap* g_domino_name_id_map;
DominoDescriptionIdMap* g_domino_description_id_map;

void dominoCommonModuleInit(void) {
    g_domino_name_id_map = dominoNameIdMap_init();
    if (!g_domino_name_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate name ID map");
    }
    g_domino_description_id_map = dominoDescriptionIdMap_init();
    if (!g_domino_description_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate description ID map");
    }
}

void dominoCommonModuleDestroy(void) {
    kv_destroy(g_domino_name_list);
    kv_init(g_domino_name_list);
    kv_destroy(g_domino_description_list);
    kv_init(g_domino_description_list);
    dominoNameIdMap_destroy(g_domino_name_id_map);
    g_domino_name_id_map = nullptr;
    dominoDescriptionIdMap_destroy(g_domino_description_id_map);
    g_domino_description_id_map = nullptr;
}

domino_name_t* dominoGetNameById(domino_name_id_t name_id) {
    if (name_id == 0) {
        return nullptr;
    }
    khint_t key = dominoNameIdMap_get(g_domino_name_id_map, name_id);
    if (kh_exist(g_domino_name_id_map, key)) {
        return &kv_A(g_domino_name_list, kh_val(g_domino_name_id_map, key));
    }
    return nullptr;
}

domino_description_t* dominoGetDescriptionById(domino_description_id_t description_id) {
    if (description_id == 0) {
        return nullptr;
    }
    khint_t key = dominoDescriptionIdMap_get(g_domino_description_id_map, description_id);
    if (kh_exist(g_domino_description_id_map, key)) {
        return &kv_A(g_domino_description_list, kh_val(g_domino_description_id_map, key));
    }
    return nullptr;
}
