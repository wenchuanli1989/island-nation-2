#ifndef DOMINO_ENGINE_HUMAN_TASK_ENTRY_H
#define DOMINO_ENGINE_HUMAN_TASK_ENTRY_H

#include "domino_shared_behavior.h"
#include "domino_shared_error_codes.h"

/** @brief 任务类型名称。 */
typedef struct DominoTaskTypeInfo {
    const char* name_str;
} DominoTaskTypeInfo;

const DominoTaskTypeInfo* dominoHumanGetTaskTypeInfo(DOMINO_TASK_TYPE type);
DOMINO_CODE dominoHumanValidateTaskRecord(const DominoTask* task_ptr);

#endif
