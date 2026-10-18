#include "entry.h"

#include "graph.h"
#include "persistence.h"
#include "state.h"

DOMINO_CODE dominoNavModuleInitBefore(void) {
    return navStateInit();
}

DOMINO_CODE dominoNavModuleInitAfter(void) {
    domino_fork_road_id_t max_fork_id = 0U;
    domino_road_line_id_t max_road_line_id = 0U;
    DOMINO_CODE result = navPersistenceValidateSegments();
    if (result == CODE_OK) {
        result = navStateValidateLoadedData(&max_fork_id, &max_road_line_id);
    }
    if (result == CODE_OK) {
        result = navGraphRebuildAdjacency();
    }
    if (result == CODE_OK) {
        navStateAdvanceNextIds(max_fork_id, max_road_line_id);
        navStateMarkCanonical();
    }
    return result;
}

DOMINO_CODE dominoNavModuleExitBefore(void) {
    if (!navStateNeedsCompaction()) {
        return CODE_OK;
    }

    DOMINO_CODE result = navStateValidateRuntimeData();
    if (result != CODE_OK) {
        return result;
    }

    navStateInvalidateAllPathCaches();
    navPersistenceCompact();
    navStateMarkCanonical();
    return CODE_OK;
}

void dominoNavModuleExitAfter(void) {
    navStateDestroy();
}
