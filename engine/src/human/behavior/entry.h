#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_ENTRY_H

#include "domino_shared_behavior.h"
#include "domino_shared_error_codes.h"

typedef struct DominoBehaviorTypeInfo {
    const char* name_str;
} DominoBehaviorTypeInfo;

const DominoBehaviorTypeInfo* dominoHumanGetBehaviorTypeInfo(DOMINO_BEHAVIOR_TYPE type);
/**
 * @brief 校验行为记录中的类型、进度、时间和专有参数，不检查目标实体是否存在。
 * @return CODE_OK 成功；behavior_ptr 为空返回 ERR_NULL_POINTER；记录字段不一致返回 ERR_INVALID_DATA。
 * @note 仅返回校验结果；由调用方结合存档、草案或运行中行为的上下文决定诊断和错误处理。
 */
DOMINO_CODE dominoHumanValidateBehaviorRecord(const DominoBehavior* behavior_ptr);

#endif
