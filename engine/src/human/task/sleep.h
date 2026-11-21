#ifndef DOMINO_ENGINE_HUMAN_TASK_SLEEP_H
#define DOMINO_ENGINE_HUMAN_TASK_SLEEP_H

#include "entry.h"

/** @file sleep.h
 * @brief 睡眠任务规则：精力达到目标且至少一个主行为（睡觉）成功结束时成功。
 * @note 尚无成功主行为时需求已被外部满足：未启动的撤销，已运行的取消，均不计实施次数。
 * 睡眠条件失效时取消主行为、任务保持 OPEN，条件恢复后由行为规划修复；遇到外部信息时完全让位。
 */
extern const DominoTaskRules g_domino_sleep_task_rules;

#endif
