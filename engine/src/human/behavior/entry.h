#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H

#include "domino_shared_human_task.h"

typedef struct DominoBehaviorTypeInfo {
    const char* name_str;
} DominoBehaviorTypeInfo;

const DominoBehaviorTypeInfo* dominoHumanGetBehaviorTypeInfo(DOMINO_BEHAVIOR_TYPE type);

#endif
