#ifndef DOMINO_ENGINE_HUMAN_TASK_DRAFT_H
#define DOMINO_ENGINE_HUMAN_TASK_DRAFT_H

#include "domino_shared_human_task.h"

/** @brief 未提交的任务计划及自有行为副本；不携带正式任务 ID、执行状态或行为 ID 清单。
 * @note 结构体复制仅复制行为地址；跨线程成功投递后转移该块所有权，接收方负责释放。
 * store 提交按值复制行为，成功或失败均不接管草案行为块，调用方随后仍须释放。
 */
typedef struct DominoTaskDraft {
    DominoBehavior* behaviors_ptr;
    domino_human_id_t owner_human_id;
    domino_runtime_ms_t planned_begin_ms;
    domino_runtime_ms_t planned_end_ms;
    DOMINO_TASK_TYPE type;
    uint8_t channel_index;
    uint8_t behavior_count;
    uint8_t priority_class;
} DominoTaskDraft;

#endif
