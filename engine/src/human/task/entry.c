#include "entry.h"

#include <stddef.h>

static const DominoTaskTypeInfo g_task_type_info[DOMINO_TASK_TYPE_COUNT] = {
    [DOMINO_TASK_TYPE_WORK] = {.name_str = "work"},
    [DOMINO_TASK_TYPE_SCHOOL] = {.name_str = "school"},
    [DOMINO_TASK_TYPE_ENTERTAINMENT] = {.name_str = "entertainment"},
    [DOMINO_TASK_TYPE_SHOPPING] = {.name_str = "shopping"},
    [DOMINO_TASK_TYPE_EXERCISE] = {.name_str = "exercise"},
    [DOMINO_TASK_TYPE_MEDICAL] = {.name_str = "medical"},
    [DOMINO_TASK_TYPE_VISIT] = {.name_str = "visit"},
    [DOMINO_TASK_TYPE_TRAVEL] = {.name_str = "travel"},
    [DOMINO_TASK_TYPE_PARADE] = {.name_str = "parade"},
    [DOMINO_TASK_TYPE_SLEEP] = {.name_str = "sleep"},
    [DOMINO_TASK_TYPE_PHONE_CALL] = {.name_str = "phone_call"},
    [DOMINO_TASK_TYPE_MESSAGE] = {.name_str = "message"},
};

const DominoTaskTypeInfo* dominoHumanGetTaskTypeInfo(DOMINO_TASK_TYPE type) {
    if (type <= DOMINO_TASK_TYPE_NONE || type >= DOMINO_TASK_TYPE_COUNT) {
        return NULL;
    }
    return &g_task_type_info[type];
}
