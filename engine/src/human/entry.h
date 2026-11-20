#ifndef DOMINO_ENGINE_HUMAN_MODULE_H
#define DOMINO_ENGINE_HUMAN_MODULE_H

#include <stdbool.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "domino_shared_types.h"

/** @file entry.h
 * @brief human 的模块对外入口；引擎入口通过本头文件编排生命周期、通知及结算，规划线程通过本头文件取得读锁并查询人物。
 * @note 不属于客户端公开 API；模块间调用不强制经过本入口，仍须遵守所调用接口的访问契约。
 * 存储层直接使用 store.h 导入动机、任务与行为，通过 state.h 串行恢复人物容器、遍历数据并绑定引用；导入仅用于可信加载。
 */

/** @brief 固定本会话 AI 配置，初始化人物容器、动机／任务／行为存储、执行器收件箱和十二条规划队列，不创建线程。
 * @pre 引擎串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 */
extern void dominoHumanModuleInit(void);

/** @brief 加载后串行准备：检查执行能力、恢复计划运行期版本，为每个人物置动机待评估标志（有有效动机的人物再置
 * 任务待规划标志），并准备首次检查机会。
 * @pre 世界加载及引用绑定完成，业务时钟冻结，所有线程尚未创建。
 * @note 不结算、不前移游标、不补做终态归并、不接受新计划、不启动任务、不投递规划、不记录业务日志。
 */
extern void dominoHumanModuleInitAfter(void);

/** @brief 在业务时钟冻结时依次创建执行器、动机、任务和行为规划线程。
 * @pre 引擎已完成 Init 及 human InitAfter，处于 STARTING；本次实例尚未启动。
 */
extern void dominoHumanModuleStart(void);

/** @brief 时钟或退出状态发布后，通知执行器收件箱及三个规划线程的统一等待对象。
 * @note 引擎在 Start 解冻、Thaw 或 Stop 时调用，普通冻结不通知；不等待回执、不消费消息。
 */
extern void dominoHumanModuleNotifyTimeChanged(void);

/** @brief 依次 join 执行器和动机／任务／行为三个规划线程，保留所有人物及运行资源。
 * @pre 引擎已进入 STOPPING 并唤醒业务线程，主循环已 join；仅 Init 的实例由引擎跳过。
 */
extern void dominoHumanModuleStop(void);

/** @brief 在引擎 Stop 阶段把全部人物的共同结算游标推进到停钟时刻，完成行为终态归并、依赖先后的任务决策及动机计次。
 * @param now_ms 引擎停钟时已校验的累计未冻结现实毫秒。
 * @pre 全部业务线程已结束，业务时钟冻结，logger 尚未 Stop；串行直访，不取得读写锁。
 * @note 不启动或恢复后继、不接受新计划、不投递规划、不登记计时器、不回收任务；Destroy 不再结算。
 */
extern void dominoHumanModuleSettle(domino_runtime_ms_t now_ms);

/** @brief 释放规划／执行资源、动机／任务／行为存储及人物容器，不停止或 join 线程。
 * @pre 引擎已完成 Stop 及保存。
 */
extern void dominoHumanModuleDestroy(void);

/** @brief 运行期读取 human 世界前取得共享锁；不能递归、升级为写锁或跨等待保留。 */
void dominoHumanReadLock(void);
/** @brief 结束本次读范围；不得继续使用借用的 Human、Data、Motivation、Task、Behavior 指针。 */
void dominoHumanReadUnlock(void);

/** @brief 按 ID 查询原始人物；不存在返回空指针，不复制数据。
 * @pre 运行期读者须先取得 human 读锁；执行器在写锁内修改；串行阶段直访。借用指针不能跨解锁或容器变化保留。
 */
DominoHuman* dominoGetHumanByID(domino_human_id_t human_id);
/** @brief 按 ID 查询原始动机；访问范围与人物查询相同。 */
DominoMotivation* dominoGetMotivationByID(domino_motivation_id_t motivation_id);
/** @brief 按 ID 查询原始任务；访问范围与人物查询相同。 */
DominoTask* dominoGetTaskByID(domino_task_id_t task_id);
/** @brief 按 ID 查询原始行为执行记录；访问范围与人物查询相同。 */
DominoBehavior* dominoGetBehaviorByID(domino_behavior_id_t behavior_id);

#ifdef DOMINO_PLAYGROUND_MODE
/* 测试验收专用：以下人物输入入口只供 playground 验证睡眠条件、精力变化和外部信息，生产构建不编译；清理测试代码时一并删除。
 * 每个入口投递独立的场景消息，不经字段表或通用字段写入；客户端没有这些入口。执行器按 FIFO 处理：先按旧值结算到本次时刻，
 * 再修改并在同一边界统一决策；人物不存在时执行器直接忽略。入队只表示收下。
 */

/** @brief 测试验收专用：提交人物睡眠执行条件的新值；条件失效后的处置由执行器随后的统一决策完成。
 * @pre human Init 已完成。
 */
void dominoHumanSubmitSleepConditionForTest(domino_human_id_t human_id, bool forbidden_flag);

/** @brief 测试验收专用：提交人物精力的新值；执行器写入实际变化后，交给仍承担责任的任务记录输入事实。
 * @pre human Init 已完成。
 * @return CODE_OK 已投递；energy 超过 DOMINO_HUMAN_NEED_MAX 返回 ERR_INVALID_PARAM，未投递。
 */
DOMINO_CODE dominoHumanSubmitEnergyForTest(domino_human_id_t human_id, uint8_t energy);

/** @brief 测试验收专用：为人物登记一条外部信息（类别与来源同次写入），或以 NONE 撤回尚未承接的外部信息。
 * @param stimulus_kind DOMINO_HUMAN_STIMULUS_KIND；由 NONE 变为非 NONE 时开启新一轮外部信息。
 * @param source_id 外部信息来源，例如来电人物 ID。
 * @pre human Init 已完成。执行器写入后，由运行任务的应激规则选择完全让位或接受插入，并安排相应规划。
 * @return CODE_OK 已投递；类别不合法返回 ERR_INVALID_PARAM，未投递。
 */
DOMINO_CODE dominoHumanSubmitStimulusForTest(domino_human_id_t human_id, uint8_t stimulus_kind, uint32_t source_id);
#endif

#endif
