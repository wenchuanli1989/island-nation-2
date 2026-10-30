#ifndef DOMINO_ENGINE_HUMAN_TASK_DRAFT_H
#define DOMINO_ENGINE_HUMAN_TASK_DRAFT_H

#include "domino_shared_human_task.h"

/** @brief 未提交的任务及自有行为副本；行为尚未分配 ID，不引用全局任务或行为池。
 * @note 结构体复制仅复制行为地址；跨线程成功投递后转移该块所有权，接收方负责释放。
 * store 提交按值复制行为，成功或失败均不接管草案行为块，调用方随后仍须释放。
 */
typedef struct DominoTaskDraft {
    DominoBehavior* behaviors_ptr;
    DominoTask task;
} DominoTaskDraft;

#endif
