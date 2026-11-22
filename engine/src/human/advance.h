#ifndef DOMINO_ENGINE_HUMAN_ADVANCE_H
#define DOMINO_ENGINE_HUMAN_ADVANCE_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_human.h"
#include "needs.h"
#include "task/entry.h"

/** @file advance.h
 * @brief 人物推进：共同结算、边界判定、只读预测，以及运行期的启动、让位、回收和状态计算。
 * @note 结算按边界分段：先结算自然需求和运行任务各通道的效果，再在边界上判定行为终态、归并行为依赖、对每项任务做统一决策，
 * 整个边界处理完才前移人物唯一的结算游标 settled_at_ms。结算不启动任务、不投递规划、不登记计时器，这些由调用方在结算后处理。
 * 真实结算与只读预测共用同一套分段和计算（宪法 9.3）。写入接口须持写权限，预测接口只读。
 */

/** @brief 把人物结算到 target_ms，每个边界处理完才前移游标。
 * @pre target_ms 不早于 settled_at_ms，否则致命退出。
 */
void dominoHumanSettle(DominoHuman* human_ptr, domino_runtime_ms_t target_ms);

/** @brief 在当前游标时刻重新判定行为和任务（条件变化或提交候选之后），不计算时间效果。 */
void dominoHumanResolveBoundary(DominoHuman* human_ptr);

/** @brief 用当前事实构造任务的决策输入。 */
DominoTaskDecisionInput dominoHumanBuildDecisionInput(const DominoHuman* human_ptr, const DominoTask* task_ptr, domino_runtime_ms_t now_ms);

/** @brief 应用任务决策：终结相关行为并归并，任务结束时释放队列位置、给动机计次并置待规划标志。
 * @note 决策为成功但仍有插入的辅助行为时，先结束自身行为、清理自身步骤并记下“完成条件已成立”，任务保持 OPEN，
 * 等插入行为结束后再提交成功。不回收任务记录。
 * @return 是否实际改变了任务或行为。
 */
bool dominoHumanApplyTaskDecision(DominoHuman* human_ptr, DominoTask* task_ptr, DominoTaskDecision decision, domino_runtime_ms_t now_ms);

/** @brief 只读预测下一次需要检查人物的时刻：行为结束、任务决策变化或截止点。
 * @note 需求降到阈值的时机不在此预测，由任务规划给出的下次规划时刻提供。
 * @return 是否有检查时刻；步数用尽时给出已推进到的边界作为保守时刻。
 */
bool dominoHumanPredictNextCheck(const DominoHuman* human_ptr, domino_runtime_ms_t* due_ms_out_ptr);

/** @brief 只读预测 target_ms 时的需求值。
 * @param work_count_ptr 累加推进的边界数，计入规划预算。
 * @return 是否在预测步数上限内推进到 target_ms。
 */
bool dominoHumanPredictNeedsAt(const DominoHuman* human_ptr, domino_runtime_ms_t target_ms, DominoHumanNeedValues* values_out_ptr,
                               uint32_t* work_count_ptr);

/** @brief 只读预测：从游标推进到 from_ms，再求精力首次不高于 threshold 的时刻。
 * @note 只剩自然衰减时直接计算；否则逐边界推进，预算内无法确定时给出已推进到的边界作为保守时刻。
 * from_ms 时已不高于阈值则输出 from_ms；在业务时间范围内不会降到阈值时输出 UINT64_MAX（视为“无”）。
 * @param step_max 最多推进的边界数；work_count_ptr 累加实际推进数。
 * @return 是否得到结果；还没推进任何边界就用尽 step_max 时返回 false。
 */
bool dominoHumanPredictEnergyDropAt(const DominoHuman* human_ptr, domino_runtime_ms_t from_ms, uint8_t threshold, uint32_t step_max,
                                    uint64_t* due_ms_out_ptr, uint32_t* work_count_ptr);

/** @brief 纯计算：从 start_ms 起新开始睡觉，在自然衰减同时作用下，精力从 energy 首次达到 goal 所需的有效睡眠时长。
 * @note 自然衰减在整分钟边界发生，结果与 start_ms 在一分钟内的位置有关。
 * @return 是否在 step_max 个边界内达到。
 */
bool dominoHumanCalcSleepDuration(uint8_t energy, domino_runtime_ms_t start_ms, uint8_t goal, uint32_t step_max,
                                  domino_runtime_ms_t* duration_out_ptr, uint32_t* work_count_ptr);

/** @brief 运行期启动：队首回应任务就绪且运行任务选择让位时先完全让位；然后为运行任务按主、辅 1、辅 2 通道启动可启动的步骤；
 * 没有运行任务时，让队首任务和它的首批步骤一起开始。
 * @return 是否启动了任何行为。
 */
bool dominoHumanStartReadyWork(DominoHuman* human_ptr, domino_runtime_ms_t now_ms);

/** @brief 回收终态或已撤销的任务，再回收已终结且没有关联任务的动机；只释放存储，不改变业务事实。 */
void dominoHumanReclaimEnded(DominoHuman* human_ptr);

/** @brief 运行任务有任一通道在执行行为时为 NORMAL，否则为 THINKING。 */
uint8_t dominoHumanComputeState(const DominoHuman* human_ptr);

/** @brief 任务是否需要行为规划：自身缺少安排且当前适合规划，或作为运行任务需要插入回应外部信息的辅助步骤。 */
bool dominoHumanTaskNeedsBehaviorPlan(const DominoHuman* human_ptr, const DominoTask* task_ptr);

/** @brief 停止阶段把全部人物结算到 now_ms。 */
void dominoHumanSettleAll(domino_runtime_ms_t now_ms);

#endif
