#include "entry.h"

#include <stddef.h>

static const DominoBehaviorTypeInfo g_behavior_type_info[DOMINO_BEHAVIOR_TYPE_COUNT] = {
    [DOMINO_BEHAVIOR_TYPE_MOVE] = {.name_str = "move"},           [DOMINO_BEHAVIOR_TYPE_EAT] = {.name_str = "eat"},
    [DOMINO_BEHAVIOR_TYPE_WASH] = {.name_str = "wash"},           [DOMINO_BEHAVIOR_TYPE_WORK] = {.name_str = "work"},
    [DOMINO_BEHAVIOR_TYPE_STUDY] = {.name_str = "study"},         [DOMINO_BEHAVIOR_TYPE_SHOP] = {.name_str = "shop"},
    [DOMINO_BEHAVIOR_TYPE_EXERCISE] = {.name_str = "exercise"},   [DOMINO_BEHAVIOR_TYPE_TREAT] = {.name_str = "treat"},
    [DOMINO_BEHAVIOR_TYPE_CHAT] = {.name_str = "chat"},           [DOMINO_BEHAVIOR_TYPE_SIGHTSEE] = {.name_str = "sightsee"},
    [DOMINO_BEHAVIOR_TYPE_PARADE] = {.name_str = "parade"},       [DOMINO_BEHAVIOR_TYPE_SLEEP] = {.name_str = "sleep"},
    [DOMINO_BEHAVIOR_TYPE_ENTERTAIN] = {.name_str = "entertain"}, [DOMINO_BEHAVIOR_TYPE_PHONE_CALL] = {.name_str = "phone_call"},
    [DOMINO_BEHAVIOR_TYPE_MESSAGE] = {.name_str = "message"},
};

const DominoBehaviorTypeInfo* dominoHumanGetBehaviorTypeInfo(DOMINO_BEHAVIOR_TYPE type) {
    if (type <= DOMINO_BEHAVIOR_TYPE_NONE || type >= DOMINO_BEHAVIOR_TYPE_COUNT) {
        return NULL;
    }
    return &g_behavior_type_info[type];
}
