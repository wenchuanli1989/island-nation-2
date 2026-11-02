#ifndef DOMINO_ENGINE_HUMAN_EXECUTOR_H
#define DOMINO_ENGINE_HUMAN_EXECUTOR_H

#include "domino_shared_types.h"

/** @file executor.h
 * @brief human 内部的执行与规划控制实现；引擎入口通过 human/entry.h 调用，模块间调用遵守本接口契约。
 */

/** @brief 初始化执行控制、执行收件箱与规划入口；不创建线程，致命错误直接终止。
 * @pre human 模块串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 */
extern void dominoHumanExecutionInit(void);

/** @brief 加载完成后串行重建首次唤醒；复用已恢复的业务身份和进度，不推进业务。
 * @pre 人物、任务和行为已加载并完成绑定，time 保持冻结，game_timer 已 Init，全部线程尚未创建。
 * @note 运行态请求身份由加载置空；新登记只属于本次 Init，异常人物不恢复调度。
 */
extern void dominoHumanExecutionInitAfter(void);

/** @brief 释放规划队列、执行留队消息和收件队列，不停止或 join 线程。
 * @pre 资源已 Init；执行、规划与 game_timer 线程均未启动或已 join，保存已完成；人物容器随后由 human 模块释放。
 */
extern void dominoHumanExecutionDestroy(void);

/** @brief 使用已备资源和调度依次创建执行和规划线程；运行线程仅按消息分批推进。
 * @pre Init 和 InitAfter 已准备基础资源及初始调度，业务时钟冻结，引擎处于 STARTING；本次实例尚未启动。
 */
extern void dominoHumanExecutionStart(void);

/** @brief 模块通知入口的内部实现，唤醒执行／规划队列等待者。
 * @pre 调用方已发布需要观察的时钟或退出状态，执行和规划队列均已 Init 且尚未 Destroy。
 * @note 不等待暂停回执，不重建已有调度。
 */
extern void dominoHumanExecutionNotifyTimeChanged(void);

/** @brief 依次 join 执行和规划线程，保留队列及其他资源；已开始的批次完整执行。
 * @pre 线程已创建，引擎已进入 STOPPING 并通知执行／规划线程，game_timer 和收件队列仍存活。
 * @note 业务线程读取 dominoEngineIsStopping()；本接口不发布退出或重复唤醒。
 * 仅 Init、未 Start 时由引擎跳过本接口；内部不重复校验生命周期。
 */
extern void dominoHumanExecutionStop(void);

/** @brief human 模块内部的统一结算实现，复用人物推进中的任务和自然需求结算逻辑。
 * @pre 调用方已保证全部业务线程结束、业务时钟冻结、世界资源有效且 logger 尚未 Stop，now_ms 为已校验的停钟时刻。
 * @note 所有人物三个通道共同结算到 now_ms，单项失败不截断其他通道；不回收终态或启动后续任务，不解除异常状态。
 */
extern void dominoHumanExecutionSettle(domino_runtime_ms_t now_ms);

#endif
