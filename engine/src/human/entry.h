#ifndef DOMINO_ENGINE_HUMAN_MODULE_H
#define DOMINO_ENGINE_HUMAN_MODULE_H

#include <stdbool.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "domino_shared_types.h"

/** @file entry.h
 * @brief human 模块的对外入口：引擎经此编排生命周期、时间通知和停止结算；规划线程经此取得读锁并查询人物。
 * @note 存储层直接使用 store.h 和 state.h 做可信加载，不经本入口。
 */

/** @brief 固定本会话 AI 配置，初始化人物容器、动机／任务／行为存储、执行器收件箱和规划队列，不创建线程。 */
extern void dominoHumanModuleInit(void);

/** @brief 加载后的串行准备：检查任务和行为类型都有规则、重建计划版本，为每个人物置待规划标志并排入首次待办。
 * @note 不结算、不前移游标、不投递规划、不启动任务。
 */
extern void dominoHumanModuleInitAfter(void);

/** @brief 在时钟冻结时依次创建执行器和动机、任务、行为三个规划线程。 */
extern void dominoHumanModuleStart(void);

/** @brief 解冻或退出状态发布后唤醒执行器和规划线程；普通冻结不调用。 */
extern void dominoHumanModuleNotifyTimeChanged(void);

/** @brief 先 join 执行器，再 join 三个规划线程；保留人物和全部运行资源。 */
extern void dominoHumanModuleStop(void);

/** @brief 停止阶段把全部人物结算到停钟时刻：行为终态、依赖归并、任务决策和动机计次。
 * @param now_ms 停钟时已校验的业务时刻。
 * @pre 业务线程全部结束，logger 尚未 Stop；串行访问，不加锁。
 * @note 不启动后续任务、不投递规划、不登记计时器、不回收任务。
 */
extern void dominoHumanModuleSettle(domino_runtime_ms_t now_ms);

/** @brief 释放规划和执行资源、存储及人物容器，不 join 线程。 */
extern void dominoHumanModuleDestroy(void);

/** @brief 运行期读者取得 human 共享读锁；不可递归、不可升级，不得跨等待持有。 */
void dominoHumanReadLock(void);
/** @brief 释放读锁；之后不得再使用借用的人物、动机、任务或行为指针。 */
void dominoHumanReadUnlock(void);

/** @brief 按 ID 查询人物，不存在返回空指针。
 * @note 运行期读者须持读锁，执行器在写锁内调用，串行阶段直接访问；借用指针不能跨解锁或容器变化保留。
 */
DominoHuman* dominoGetHumanById(domino_human_id_t human_id);
/** @brief 按 ID 查询动机；访问约定同 dominoGetHumanById。 */
DominoMotivation* dominoGetMotivationById(domino_motivation_id_t motivation_id);
/** @brief 按 ID 查询任务；访问约定同 dominoGetHumanById。 */
DominoTask* dominoGetTaskById(domino_task_id_t task_id);
/** @brief 按 ID 查询行为记录；访问约定同 dominoGetHumanById。 */
DominoBehavior* dominoGetBehaviorById(domino_behavior_id_t behavior_id);

#ifdef DOMINO_PLAYGROUND_MODE
/* 测试验收专用：睡眠条件、精力、外部信息三个人物输入入口，只在 playground 编译，清理测试代码时一并删除。
 * 每个入口向执行器投递一条独立消息；执行器先按旧值结算到当前时刻，再写入变化并在同一边界统一决策，人物不存在时忽略。
 */

/** @brief 测试验收专用：提交睡眠条件的新值。 */
void dominoHumanSubmitSleepConditionForTest(domino_human_id_t human_id, bool forbidden_flag);

/** @brief 测试验收专用：提交精力的新值。
 * @return CODE_OK 已投递；energy 超过 DOMINO_HUMAN_NEED_MAX 返回 ERR_INVALID_PARAM。
 */
DOMINO_CODE dominoHumanSubmitEnergyForTest(domino_human_id_t human_id, uint8_t energy);

/** @brief 测试验收专用：登记一条外部信息（类别和来源同时写入），或以 NONE 撤回尚未承接的外部信息。
 * @param source_id 外部信息来源，例如来电人物 ID。
 * @return CODE_OK 已投递；类别不合法返回 ERR_INVALID_PARAM。
 */
DOMINO_CODE dominoHumanSubmitStimulusForTest(domino_human_id_t human_id, uint8_t stimulus_kind, uint32_t source_id);
#endif

#endif
