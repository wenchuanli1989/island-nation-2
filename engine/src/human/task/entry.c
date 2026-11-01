#include "entry.h"

#include "sleep.h"

static const DominoTaskOps* const g_task_ops[DOMINO_TASK_TYPE_COUNT] = {
    [DOMINO_TASK_TYPE_SLEEP] = &domino_sleep_task_ops,
};

const DominoTaskOps* dominoHumanGetTaskOps(DOMINO_TASK_TYPE type) {
    if (type <= DOMINO_TASK_TYPE_NONE || type >= DOMINO_TASK_TYPE_COUNT) {
        return nullptr;
    }
    return g_task_ops[type];
}
