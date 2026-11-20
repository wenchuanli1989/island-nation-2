#ifndef DOMINO_ENGINE_HUMAN_TASK_SLEEP_H
#define DOMINO_ENGINE_HUMAN_TASK_SLEEP_H

#include "entry.h"

/** @file sleep.h
 * @brief 睡眠任务规则：精力不低于任务目标且至少一个所属主行为（主通道睡眠）成功结束时成功。
 * @note 外部输入在尚无成功主行为时补足精力：未启动计划依法撤销，已运行任务取消，均不计实施次数；
 * 睡眠条件失效时取消主行为并保持任务 OPEN 等待修复，条件恢复后由行为规划器修复剩余步骤。
 * 遇到待处理外部信息时完全让位，回应任务结束后再修复剩余睡眠。
 */
extern const DominoTaskRules domino_sleep_task_rules;

#endif
