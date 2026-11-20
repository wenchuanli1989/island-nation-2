#ifndef DOMINO_ENGINE_HUMAN_EXECUTOR_H
#define DOMINO_ENGINE_HUMAN_EXECUTOR_H

#include <stdint.h>

#include "../queue/message.h"
#include "domino_shared_types.h"

/** @file executor.h
 * @brief human 执行器：运行期唯一写入者。按待规划标志、下次规划时刻和行为计划缺口发起三类规划，消费六条结果队列和业务收件箱，
 * 复核并提交候选，调度启动、结算、收尾和唤醒；不复评动机，不复核规划判断的依据。
 * @note 执行器按有限批次处理：每条结果队列最多 8 条、收件箱 16 条（合计不超过 64），另处理 16 个本地待办；
 * 出队时不持 human 锁，处理时持写锁调用具体业务接口。引擎入口通过 human/entry.h 调用，模块间调用遵守本接口契约。
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

/** @brief 初始化执行器收件箱和三类规划器资源；不创建线程。 */
void dominoHumanExecutionInit(void);

/** @brief 加载后串行准备：建立人物运行数组、恢复计划运行期版本、重建任务记录计数，为每个人物置动机待评估标志
 * （有有效动机的人物再置任务待规划标志），并准备首次本地待办。
 * @note 不结算、不前移游标、不启动任务、不投递规划；首次请求由启动后的执行器投递。
 */
void dominoHumanExecutionInitAfter(void);

/** @brief 依次创建执行器、动机、任务和行为规划线程。 */
void dominoHumanExecutionStart(void);

/** @brief 唤醒执行器收件箱和三个规划线程的等待对象。 */
void dominoHumanExecutionNotifyTimeChanged(void);

/** @brief 依次 join 执行器和三个规划线程，保留全部队列及留队载荷。 */
void dominoHumanExecutionStop(void);

/** @brief 释放规划队列（整块释放留队候选）、收件箱、运行数组和待办队列并复位。 */
void dominoHumanExecutionDestroy(void);

/** @brief 非阻塞投递业务收件消息；失败致命退出。
 * @note 计时通知由 game_timer 直接投递到收件箱；当前经本接口投递的只有 playground 中测试验收专用的人物输入。
 */
void dominoHumanExecutionSubmit(DominoHumanExecutionMessage* message_ptr);

/** @brief 读取执行器统计；须在执行线程 join 之后调用。 */
DominoHumanExecutorStats dominoHumanExecutionGetStats(void);

#ifdef DOMINO_PLAYGROUND_MODE
/** @brief 仅 playground：在调用方持有写权限时串行处理一条规划结果，复用运行期审核与提交路径。 */
void dominoHumanExecutionProcessResultForTest(uint8_t planner_kind, uint8_t priority, DominoHumanPlanResultMessage* result_ptr);
/** @brief 仅 playground：串行推进一个人物到当前时刻并完成运行期调度（不经过收件箱）。 */
void dominoHumanExecutionAdvanceForTest(domino_human_id_t human_id);
/** @brief 仅 playground，测试验收专用：串行处理一条人物输入（睡眠条件、精力或外部信息），复用运行期的结算、判定与收尾。
 * @pre message_ptr->type 为三类测试验收专用的人物输入之一。
 */
void dominoHumanExecutionProcessMessageForTest(const DominoHumanExecutionMessage* message_ptr);
#endif

#endif
