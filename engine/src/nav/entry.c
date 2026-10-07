#include "entry.h"

#include "../logger/entry.h"
#include "graph.h"
#include "persistence.h"
#include "state.h"

DOMINO_CODE dominoNavModuleInitBefore(void) {
    DOMINO_CODE result = navStateInit();
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR, "failed to initialize navigation state (code=%d)", result);
        return result;
    }

    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_INFO, "navModuleInitBefore");
    return CODE_OK;
}

DOMINO_CODE dominoNavModuleInitAfter(void) {
    if (!navStateIsReady()) {
        return ERR_NOT_INITIALIZED;
    }

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
    } else {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_ERROR,
                          "failed to validate persisted navigation layout or rebuild runtime state (code=%d)", result);
    }
    return result;
}

DOMINO_CODE dominoNavModuleExitBefore(void) {
    if (!navStateIsReady()) {
        return ERR_NOT_INITIALIZED;
    }
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
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_NAV, DOMINO_LOG_LEVEL_INFO, "navModuleExitAfter");
}
