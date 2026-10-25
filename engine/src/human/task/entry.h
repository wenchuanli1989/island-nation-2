#ifndef DOMINO_ENGINE_HUMAN_TASK_ENTRY_H
#define DOMINO_ENGINE_HUMAN_TASK_ENTRY_H

#include "domino_shared_behavior.h"
#include "domino_shared_error_codes.h"

/** @brief 任务类型名称。 */
typedef struct DominoTaskTypeInfo {
    const char* name_str;
} DominoTaskTypeInfo;

const DominoTaskTypeInfo* dominoHumanGetTaskTypeInfo(DOMINO_TASK_TYPE type);
/**
 * @brief 校验任务记录及其行为内容，不检查跨任务或人物关系。
 * @return CODE_OK 成功；task_ptr 为空返回 ERR_NULL_POINTER；记录缺少行为块或字段不一致返回 ERR_INVALID_DATA。
 * @note 仅返回校验结果；由调用方结合存档、草案或运行中任务的上下文决定诊断和错误处理。
 */
DOMINO_CODE dominoHumanValidateTaskRecord(const DominoTask* task_ptr);

#endif
