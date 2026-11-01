#ifndef DOMINO_ENGINE_HUMAN_EXECUTOR_H
#define DOMINO_ENGINE_HUMAN_EXECUTOR_H

#include "domino_shared_types.h"

/** @brief 初始化执行控制、执行收件箱与规划入口；不创建线程，致命错误直接终止。
 * @pre 引擎和 human 模块串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 */
extern void dominoHumanExecutionInit(void);

/** @brief 释放规划队列、执行留队消息和收件队列，不停止或 join 线程。
 * @pre 资源已 Init；执行、规划与 game_timer 线程均未启动或已 join，保存已完成；人物容器随后由 human 模块释放。
 */
extern void dominoHumanExecutionDestroy(void);

/** @brief 使用已备资源依次创建执行和规划线程，不预扫世界；首次运行分批恢复执行并建立调度。
 * @pre Init 已准备全部基础资源，业务时钟冻结，引擎处于 STARTING；本次实例尚未启动。
 * @note 执行器支持范围在实际推进人物时检查；持久事实复用可信加载与受控提交的保证。
 */
extern void dominoHumanExecutionStart(void);

/** @brief 时钟或退出状态发布后唤醒执行／规划线程；不等待暂停回执，不重建已有调度。 */
extern void dominoHumanExecutionNotifyTimeChanged(void);

/** @brief 依次 join 执行和规划线程，保留队列及其他资源；已开始的批次完整执行。
 * @pre 线程已创建，引擎已进入 STOPPING 并通知执行／规划线程，game_timer 和收件队列仍存活。
 * @note 业务线程读取 dominoEngineIsStopping()；本接口不发布退出或重复唤醒。
 * 仅 Init、未 Start 时由引擎跳过本接口；内部不重复校验生命周期。
 */
extern void dominoHumanExecutionStop(void);

/** @brief 所有业务线程退出后，将正常人物三个通道共同结算到冻结时刻；不回收终态或启动后续任务，异常人物跳过。
 * @pre human 和 game_timer Stop 已完成；世界仍存活，调用方独占人物写入权。
 */
extern void dominoHumanExecutionSettle(domino_runtime_ms_t now_ms);

#endif
