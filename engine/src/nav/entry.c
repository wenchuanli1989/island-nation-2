#include "entry.h"

#include "graph.h"
#include "layout.h"
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
        navStateInvalidateAllPathCaches();
        navLayoutCompact();
        navStateMarkCanonical();
    }
}

void dominoNavModuleDestroy(void) {
    navStateDestroy();
}
