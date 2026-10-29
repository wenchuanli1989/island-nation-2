#include "entry.h"

#include "graph.h"
#include "persistence.h"
#include "state.h"

void dominoNavModuleInit(void) {
    navStateInit();
}

void dominoNavModuleInitAfter(void) {
    navStateValidateLoadedCapacity();
    navGraphRebuildAdjacency();
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

void dominoNavModuleDestroy(void) {
    navStateDestroy();
}
