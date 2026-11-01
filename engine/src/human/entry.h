#ifndef DOMINO_ENGINE_HUMAN_MODULE_H
#define DOMINO_ENGINE_HUMAN_MODULE_H

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"

/** @brief 初始化人物容器、任务存储、执行／规划队列及控制资源，不创建线程。
 * @pre 引擎串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 */
extern void dominoHumanModuleInit(void);

/** @brief 在业务时钟冻结时依次创建执行和规划线程，不重新分配基础资源。
 * @pre 引擎已完成 Init，处于 STARTING；本次实例尚未启动。
 */
extern void dominoHumanModuleStart(void);

/** @brief 依次 join 执行线程和规划线程，保留所有人物及运行资源。
 * @pre 线程已创建，引擎已进入 STOPPING 并唤醒业务线程；game_timer 和投递目标仍有效。
 * @note 仅 Init、未 Start 的实例由引擎跳过本接口；内部不重复校验生命周期。
 */
extern void dominoHumanModuleStop(void);

/** @brief 释放规划／执行资源、任务存储及人物容器，不停止或 join 线程。
 * @pre 引擎已完成 Stop 及保存；运行过的实例已 join 全部业务线程并在保存前完成统一结算。
 * @note 支持仅 Init 的资源释放；人物容器须保留到运行资源释放完成，顺序由引擎保证。
 */
extern void dominoHumanModuleDestroy(void);

/** @brief 运行期读取 human 世界前取得共享锁；不能递归、升级为写锁或跨等待保留。
 * @pre human 已 Init；调用者在引擎 Stop 前结束全部访问，内部业务线程可完成 STOPPING 末批读取。
 * @note Freeze 不替代读锁。加载、保存和线程全部结束后的结算使用 state.h 串行直访，无需此接口。
 */
void dominoHumanReadLock(void);
/** @brief 结束本次读范围；不得继续使用借用的 Human、Data、Task、Behavior 指针。 */
void dominoHumanReadUnlock(void);

/** @brief 按 ID 查询原始人物；不存在返回空指针，不复制数据。
 * @pre 运行期读者须先取得 human 读锁，只读取人物及关联 Data、Task、Behavior，不通过指针修改。
 * 执行器在写锁内修改；加载、线程结束后的结算、保存和销毁串行直访。
 * @note 查询复用调用方已建立的访问范围，不再次加锁。借用指针不能跨解锁或容器扩容、删除、重排保留。
 * human 读锁不覆盖关联 host 对象；访问 movable_object 指向的数据仍遵守 host 的访问契约。
 */
DominoHuman* dominoGetHumanByID(domino_human_id_t human_id);
/** @brief 按 ID 查询原始任务；不存在返回空指针，访问范围与人物查询相同。 */
DominoTask* dominoGetTaskByID(domino_task_id_t task_id);
/** @brief 按 ID 查询原始行为；不存在返回空指针，访问范围与人物查询相同。 */
DominoBehavior* dominoGetBehaviorByID(domino_behavior_id_t behavior_id);

#endif
