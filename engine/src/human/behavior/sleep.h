#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_SLEEP_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_SLEEP_H

#include "entry.h"

/** @file sleep.h
 * @brief 睡觉行为：ACTIVE 且睡眠条件成立时按有效时长累计进度，每 6000ms 有效睡眠恢复 1 点精力；达到目标有效时长即成功。
 * @note 等待、冻结和未启动阶段不算有效睡眠时间；行为时长不能用计划开始／结束时间差替代。
 */
extern const DominoBehaviorRules domino_sleep_behavior_rules;

#endif
