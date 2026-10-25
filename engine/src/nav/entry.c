#include "entry.h"

#include "graph.h"
#include "persistence.h"
#include "state.h"

void dominoNavModuleInitBefore(void) {
    navStateInit();
}

void dominoNavModuleInitAfter(void) {
    domino_fork_road_id_t max_fork_id = 0U;
    domino_road_line_id_t max_road_line_id = 0U;
    navPersistenceValidateSegments();
    navStateValidateLoadedData(&max_fork_id, &max_road_line_id);
    navGraphRebuildAdjacency();
    navStateAdvanceNextIds(max_fork_id, max_road_line_id);
    navStateMarkCanonical();
}

void dominoNavModuleDestroyBefore(void) {
    if (navStateNeedsCompaction()) {
        navStateValidateRuntimeData();

        navStateInvalidateAllPathCaches();
        navPersistenceCompact();
        navStateMarkCanonical();
    }
}

void dominoNavModuleDestroyAfter(void) {
    navStateDestroy();
}
