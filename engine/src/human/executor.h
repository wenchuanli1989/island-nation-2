#ifndef DOMINO_ENGINE_HUMAN_EXECUTOR_H
#define DOMINO_ENGINE_HUMAN_EXECUTOR_H

#include <stdint.h>

#include "../queue/message.h"
#include "domino_shared_types.h"

/** @file executor.h
 * @brief human 执行器：运行期人物世界的唯一写入者。按待规划标志、下次规划时刻和行为计划缺口发起规划，
 * 复核并提交规划结果，推进结算、启动任务和登记唤醒；不复评动机，不复核规划判断的依据（宪法第六章）。
 * @note 按有限批次处理：每条结果队列 result_quota 条、收件箱 inbox_quota 条、本地待办 todo_quota 个；出队时不持锁，处理时持写锁。
 */

/** @brief 执行器性能统计；只由执行线程写入，join 后读取。 */
typedef struct DominoHumanExecutorStats {
    uint64_t batch_count;
    uint64_t result_count_list[DOMINO_HUMAN_PLANNER_COUNT][DOMINO_HUMAN_PLAN_PRIORITY_COUNT];
    uint64_t stale_result_count;  ///< 身份不匹配的旧或重复结果。
    uint64_t accepted_candidate_count;
    uint64_t rejected_candidate_count;
    uint64_t posted_request_count_list[DOMINO_HUMAN_PLANNER_COUNT];
    uint64_t todo_count;
    uint64_t max_result_wait_ns;  ///< 结果从投递到出队的最大单调等待。
    uint64_t write_lock_ns;       ///< 写锁持有时间合计。
} DominoHumanExecutorStats;

/** @brief 初始化收件箱和规划器资源，不创建线程。 */
void dominoHumanExecutorInit(void);

/** @brief 加载后的串行准备：建立运行数组，重建计划版本和行为记录计数，为每个人物置待规划标志并排入首次待办。
 * @note 不结算、不启动任务、不投递规划；这些由启动后的执行器完成。
 */
void dominoHumanExecutorInitAfter(void);

/** @brief 创建执行器线程，再创建三个规划线程。 */
void dominoHumanExecutorStart(void);

/** @brief 唤醒执行器和三个规划线程。 */
void dominoHumanExecutorNotifyTimeChanged(void);

/** @brief 先 join 执行器（规划请求的唯一生产者），再 join 三个规划线程；保留全部队列。 */
void dominoHumanExecutorStop(void);

/** @brief 释放规划队列（含留队候选）、收件箱、运行数组和待办队列并复位。 */
void dominoHumanExecutorDestroy(void);

/** @brief 执行器统计；须在执行线程 join 之后读取。 */
DominoHumanExecutorStats dominoHumanExecutorGetStats(void);

#endif
