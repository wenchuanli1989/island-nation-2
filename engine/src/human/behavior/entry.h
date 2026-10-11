#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H

#include "domino_shared_behavior.h"
#include "domino_shared_error_codes.h"

typedef struct DominoBehaviorTypeInfo {
    const char* name_str;
} DominoBehaviorTypeInfo;

const DominoBehaviorTypeInfo* dominoHumanGetBehaviorTypeInfo(DOMINO_BEHAVIOR_TYPE type);
DOMINO_CODE dominoHumanValidateBehaviorRecord(const DominoBehavior* behavior_ptr);

#endif
