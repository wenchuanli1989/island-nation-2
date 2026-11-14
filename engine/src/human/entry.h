#ifndef DOMINO_ENGINE_HUMAN_MODULE_H
#define DOMINO_ENGINE_HUMAN_MODULE_H

#include "domino_engine.h"
#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "domino_shared_types.h"

/** @file entry.h
 * @brief human 的模块对外入口；引擎入口通过本头文件编排生命周期、通知及结算。
 * @note 不属于客户端公开 API；模块间调用不强制经过本入口，仍须遵守所调用接口的访问契约。
 * 存储层直接使用 store.h 导入动机、任务与行为，通过 state.h 串行恢复人物容器、遍历数据并绑定引用。
 */

/** @brief 初始化人物容器、动机／任务／行为存储、执行收件箱和十二个规划队列及控制资源，不创建线程。
 * @pre 引擎串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 */
extern void dominoHumanModuleInit(void);

/** @brief 加载后确认活动任务执行能力，并从人物、任务及行为事实重建本会话的首次唤醒。
 * @pre human、game_timer 已 Init，世界加载及引用绑定完成，业务时钟冻结，所有线程尚未创建。
 * @note 串行直访，不取得读写锁；不结算、不启动任务、不提交规划、不记录业务日志。
 * 旧队列及请求身份不恢复；全部人物按持久化条件、计划、进度及重试时刻重建待办与通知。
 */
extern void dominoHumanModuleInitAfter(void);

/** @brief 在业务时钟冻结时依次创建执行器、动机、任务及行为规划线程，不重新分配基础资源。
 * @pre 引擎已完成 Init 及 human InitAfter，处于 STARTING；本次实例尚未启动。
 */
extern void dominoHumanModuleStart(void);

/** @brief 时钟或退出状态发布后，通知 human 的执行和规划队列等待者重新检查状态。
 * @pre human 已 Init，队列尚未 Destroy；调用方已先发布需要业务线程观察的时钟或退出状态。
 * @note 引擎在 Start 解冻、Thaw 或 Stop 时调用，普通冻结不通知；time 的冻结等待者由 time 模块先行唤醒。
 * 不改变时钟、不等待暂停回执、不消费消息、不关闭入队或重建调度。
 */
extern void dominoHumanModuleNotifyTimeChanged(void);

/** @brief 依次 join 执行器、动机、任务及行为规划线程，保留所有人物及运行资源。
 * @pre 线程已创建，引擎已进入 STOPPING 并唤醒业务线程；game_timer 和投递目标仍有效。
 * @note 本接口不结算人物；引擎结束全部业务线程后另行调用 dominoHumanModuleSettle。
 * 仅 Init、未 Start 的实例由引擎跳过本接口；内部不重复校验生命周期。
 */
extern void dominoHumanModuleStop(void);

/** @brief 在引擎 Stop 阶段统一结算全部人物、三个通道、执行依赖和自然需求。
 * @param now_ms 引擎停钟时已校验的累计未冻结现实毫秒，所有人物共同结算到此时刻。
 * @pre 引擎保证全部业务线程已结束，业务时钟保持冻结，世界资源有效且 logger 尚未 Stop。
 * @note 引擎仅对运行过的实例调用一次；仅 Init 的实例跳过。串行直访人物数据，不再取得读写锁或重复校验生命周期。
 * 单项任务失败仍完成其他通道及自然需求的结算；不消费留队消息、不回收终态、不启动后续任务或登记计时器。
 * Destroy 阶段不再结算。
 */
extern void dominoHumanModuleSettle(domino_runtime_ms_t now_ms);

/** @brief 释放规划／执行资源、动机／任务／行为存储及人物容器，不停止或 join 线程。
 * @pre 引擎已完成 Stop 及保存；运行过的实例已在 Stop 阶段通过 dominoHumanModuleSettle 完成统一结算。
 * @note 支持仅 Init 的资源释放；人物容器须保留到运行资源释放完成，顺序由引擎保证。
 */
extern void dominoHumanModuleDestroy(void);

/** @brief 运行期读取 human 世界前取得共享锁；不能递归、升级为写锁或跨等待保留。
 * @pre human 已 Init；调用者在引擎 Stop 前结束全部访问，内部业务线程可完成 STOPPING 末批读取。
 * @note Freeze 不替代读锁。加载、保存和线程全部结束后的结算使用 state.h 串行直访，无需此接口。
 */
void dominoHumanReadLock(void);
/** @brief 结束本次读范围；不得继续使用借用的 Human、Data、Motivation、Task、Behavior 指针。 */
void dominoHumanReadUnlock(void);

/** @brief 按 ID 查询原始人物；不存在返回空指针，不复制数据。
 * @pre 运行期读者须先取得 human 读锁，只读取人物及关联 Data、Motivation、Task、Behavior，不通过指针修改。
 * 执行器在写锁内修改；加载、线程结束后的结算、保存和销毁串行直访。
 * @note 查询复用调用方已建立的访问范围，不再次加锁。借用指针不能跨解锁或容器扩容、删除、重排保留。
 * human 读锁不覆盖关联 host 对象；访问 movable_object 指向的数据仍遵守 host 的访问契约。
 */
DominoHuman* dominoGetHumanByID(domino_human_id_t human_id);
DominoMotivation* dominoGetMotivationByID(domino_motivation_id_t motivation_id);
/** @brief 按 ID 查询原始任务；不存在返回空指针，访问范围与人物查询相同。 */
DominoTask* dominoGetTaskByID(domino_task_id_t task_id);
/** @brief 按 ID 查询原始行为；不存在返回空指针，访问范围与人物查询相同。 */
DominoBehavior* dominoGetBehaviorByID(domino_behavior_id_t behavior_id);

/** @brief 主循环只读查询：内部取得一次短读锁并复制有界值，不推进 AI。 */
void dominoHumanQueryNpc(uint64_t client_id, const DominoNpcRequest* request, DominoNpcResult* result);
/** @brief 已由接入层校验大小和会话的显示更新；执行器提交时复核人物权限和版本。 */
void dominoHumanRequestNameChange(uint64_t client_id, const DominoNpcRequest* request);
/** @brief 引擎内部只改变输入事实；不属于客户端 API，调用方在 Stop 前结束投递。 */
DOMINO_CODE dominoHumanUpdateSleepCondition(domino_human_id_t human_id, bool allowed);
DOMINO_CODE dominoHumanUpdateEnergy(domino_human_id_t human_id, uint8_t energy);
#endif
