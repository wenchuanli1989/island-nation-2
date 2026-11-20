#ifndef DOMINO_ENGINE_HUMAN_ADVANCE_H
#define DOMINO_ENGINE_HUMAN_ADVANCE_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_human.h"
#include "needs.h"
#include "task/entry.h"

/** @file advance.h
 * @brief 人物推进：共同结算、边界判定与收尾、只读预测，以及运行期的启动、让位、回收和状态计算。
 * @note 结算与 Stop 共用：先分段结算自然需求和运行任务各通道效果，再在每个边界判定行为结果、提交终态并归并行为依赖，
 * 最后对每项任务调用统一任务决策并应用；整个边界处理完才前移人物唯一游标 settled_at_ms。
 * 同一时间只有一个运行任务，行为依赖只在同一任务内；任务之间没有依赖，不需要排序。
 * 结算流程不启动后继、不让位、不派发规划、不登记计时器；任务进入终态或被撤销时只置动机和任务待规划标志，
 * 启动、规划和唤醒由运行期调用方在结算之后处理。
 * 写入入口要求调用方持有 human 写权限（执行器写锁或串行阶段）；预测入口只读，调用方持读锁或写锁。
 */

/** @brief 待处理外部信息对规划的需要。 */
typedef enum {
    DOMINO_HUMAN_STIMULUS_NEED_NONE = 0,  ///< 没有待处理外部信息。
    DOMINO_HUMAN_STIMULUS_NEED_INSERT,    ///< 运行任务接受插入：需要为它规划插入的辅助步骤。
    DOMINO_HUMAN_STIMULUS_NEED_TASK,      ///< 没有运行任务或运行任务选择完全让位：需要回应任务。
} DOMINO_HUMAN_STIMULUS_NEED;

/** @brief 人物当前运行任务；没有返回空指针。 */
DominoTask* dominoHumanActiveTask(const DominoHumanData* data_ptr);

/** @brief 把人物共同结算到 target_ms：效果、行为终态及依赖归并、任务决策及收尾在每个边界完成后才前移游标。
 * @pre target_ms 不早于 settled_at_ms，否则致命退出；目标等于游标时不计算时间效果。
 */
void dominoHumanSettle(DominoHuman* human_ptr, domino_runtime_ms_t target_ms);

/** @brief 在当前游标时刻重新判定行为结果和任务决策（条件变更或候选提交之后），不计算时间效果、游标不变。 */
void dominoHumanResolveBoundary(DominoHuman* human_ptr);

/** @brief 按当前事实为任务构造决策输入；行为取各通道当前未终结记录。 */
DominoTaskDecisionInput dominoHumanBuildDecisionInput(const DominoHuman* human_ptr, const DominoTask* task_ptr, domino_runtime_ms_t now_ms);

/** @brief 推进层协调后提交获准决策：终结行为与任务事实同次归并，任务结束时处理队列位置与动机实施。
 * @note 成功时仍有插入的辅助行为，则先终结自身其余行为、清理自身步骤并记下完成条件已成立，任务保持 OPEN，
 * 等插入行为结束后再提交成功。不回收任务记录；回收由运行期调用方在边界处理之后进行。
 * @return 是否实际改变了任务或行为状态。
 */
bool dominoHumanApplyTaskDecision(DominoHuman* human_ptr, DominoTask* task_ptr, DominoTaskDecision decision, domino_runtime_ms_t now_ms);

/** @brief 待处理外部信息需要什么规划：运行任务按自身应激规则选择接受插入或完全让位；只读。 */
uint8_t dominoHumanStimulusNeed(const DominoHuman* human_ptr);

/** @brief 只读预测：从游标出发，返回下一次需要重新检查人物的时刻（行为结束、任务决策变化或任务截止点等执行事实）。
 * @note 不预测动机复评；需求达到阈值时的检查时机只由任务规划给出的下次规划时刻提供。
 * @return 是否存在检查时刻；无法在预测步数内确定时返回最后推进到的保守边界。
 */
bool dominoHumanPredictNextCheck(const DominoHuman* human_ptr, domino_runtime_ms_t* due_ms_out_ptr);

/** @brief 只读预测 target_ms 时的需求值；与真实结算使用相同分段与基础计算，不修改世界或游标。
 * @param work_count_ptr 累加本次推进的边界数，供规划预算统计。
 * @return 是否在预测步数上限内完成。
 */
bool dominoHumanPredictNeedsAt(const DominoHuman* human_ptr, domino_runtime_ms_t target_ms, DominoHumanNeedValues* values_out_ptr,
                               uint32_t* work_count_ptr);

/** @brief 规划用只读预测：从游标推进到 from_ms，再找精力首次不高于 threshold 的时刻；与真实结算共用分段与基础计算。
 * @note 没有推进中的行为时直接按自然衰减计算；有推进中的行为时逐个边界推进，预算内无法确定的给出已推进到的边界作为保守时刻。
 * from_ms 时已不高于阈值则返回 from_ms；业务时间上限内不会降到阈值时返回 UINT64_MAX，由调用方视为“无”。
 * @param step_max 本次最多推进的边界数；work_count_ptr 累加实际推进的边界数，供规划预算统计。
 * @return 是否得到结果；推进到 from_ms 之前或之后尚未推进任何边界就用尽 step_max 时返回 false。
 */
bool dominoHumanPredictEnergyDropAt(const DominoHuman* human_ptr, domino_runtime_ms_t from_ms, uint8_t threshold, uint32_t step_max,
                                    uint64_t* due_ms_out_ptr, uint32_t* work_count_ptr);

/** @brief 规划用纯计算：从 start_ms 时的 energy 起，自然衰减与一个在 start_ms 启动的新睡觉行为共同作用下精力首次达到 goal 所需的有效睡眠时长。
 * @note 自然需求按业务时间整分钟边界衰减，因此结果与 start_ms 在一分钟内的位置有关；推进不超过 UINT32_MAX。
 * @return 是否在 step_max 个边界内达到。
 */
bool dominoHumanCalcSleepDuration(uint8_t energy, domino_runtime_ms_t start_ms, uint8_t goal, uint32_t step_max,
                                  domino_runtime_ms_t* duration_out_ptr, uint32_t* work_count_ptr);

/** @brief 运行期：队首回应任务就绪且运行任务选择让位时先完全让位；再为运行任务按主、辅 1、辅 2 的顺序启动可启动步骤，
 * 没有运行任务时让队首任务与其首批可启动步骤一起开始。
 * @return 是否启动了任何行为。
 */
bool dominoHumanStartReadyWork(DominoHuman* human_ptr, domino_runtime_ms_t now_ms);

/** @brief 运行期：回收终态及已撤销的任务记录，再回收已终结且没有关联任务的动机；回收只释放存储，不改变业务事实。 */
void dominoHumanRecycleEnded(DominoHuman* human_ptr);

/** @brief 根据运行任务各通道当前行为计算 NORMAL／THINKING。 */
uint8_t dominoHumanComputeState(const DominoHuman* human_ptr);

/** @brief 任务是否需要行为规划：自身缺少安排且当前适合规划，或作为运行任务需要为外部信息插入辅助步骤。 */
bool dominoHumanTaskNeedsBehaviorPlan(const DominoHuman* human_ptr, const DominoTask* task_ptr);

/** @brief Stop 阶段串行结算全部人物到 now_ms；单项结果不截断其他人物和通道。 */
void dominoHumanSettleAll(domino_runtime_ms_t now_ms);

#endif
