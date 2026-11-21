#ifndef DOMINO_ENGINE_HUMAN_BEHAVIOR_SLEEP_H
#define DOMINO_ENGINE_HUMAN_BEHAVIOR_SLEEP_H

#include "entry.h"

/** @file sleep.h
 * @brief 睡觉行为：ACTIVE 且睡眠条件成立时累计有效进度，每 DOMINO_HUMAN_SLEEP_ENERGY_POINT_MS 恢复 1 点精力，达到目标时长即成功。
 * @note 等待、冻结和未启动阶段不计入有效睡眠。
 */
extern const DominoBehaviorRules g_domino_sleep_behavior_rules;

#endif
