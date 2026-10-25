#ifndef DOMINO_ENGINE_HUMAN_EXECUTOR_H
#define DOMINO_ENGINE_HUMAN_EXECUTOR_H

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"

/** @brief 初始化执行控制、显式睡眠许可、执行收件箱、唤醒关联与规划入口；不创建线程，致命错误直接终止。
 * @pre 引擎和 human 模块串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 */
extern void dominoHumanExecutionInit(void);

/** @brief 释放留队消息、收件队列、唤醒关联、同步对象与睡眠许可，不停止或 join 线程。
 * @pre 资源已 Init；执行、规划与 timer 线程均未启动或已 join，保存已完成；人物容器随后由 human 模块释放。
 */
extern void dominoHumanExecutionDestroy(void);

/** @brief 在冻结时刻校验已加载活动睡眠，创建执行线程并等待启动就绪；首次运行分批建立调度。
 * @pre Init 已准备全部基础资源，世界冻结，引擎处于 STARTING；规划线程由 human 模块随后启动。
 */
extern void dominoHumanExecutionStart(void);

/** @brief 时钟或退出状态发布后唤醒执行／规划线程；不等待暂停回执，不重建已有调度。 */
extern void dominoHumanExecutionNotifyTimeChanged(void);

/** @brief join 执行线程，保留队列及其他资源；已开始的批次完整执行。
 * @pre 线程已创建，引擎已进入 STOPPING 并通知执行／规划线程，timer 和收件队列仍存活。
 * @note 执行线程读取 dominoEngineIsStopping()；本接口不发布退出、重复唤醒或停止规划线程。
 * 仅 Init、未 Start 时由引擎跳过本接口；内部不重复校验生命周期。
 */
extern void dominoHumanExecutionStop(void);

/** @brief 所有业务线程退出后统一结算到冻结时刻；不释放资源，无法形成一致世界状态时直接终止。
 * @pre human 和 timer Stop 已完成；世界与睡眠许可仍存活，调用方独占人物写入权。
 */
extern void dominoHumanExecutionSettle(domino_runtime_ms_t now_ms);

/** @brief 显式授予同楼睡眠许可并允许后续自主睡眠；运行时由执行线程处理，许可不进入存档。
 * @pre 引擎公开入口已确认处于 INITIALIZED 或 RUNNING，且调用方为登记的客户端线程；与生命周期操作串行。
 * @note 普通业务拒绝只返回结果，由引擎公开入口记录。自主睡眠仅在三槽为空、无任何非终态任务且有容量时选择主通道；
 * 高精力只登记下一精力阈值。
 */
extern DOMINO_CODE dominoHumanExecutionAuthorizeSleep(domino_human_id_t human_id, domino_building_id_t building_id);

/** @brief 提交并许可单行为睡眠；运行中完成当次人物推进后才返回成功。
 * @pre 引擎公开入口已确认处于 INITIALIZED 或 RUNNING，且调用方为登记的客户端线程；与生命周期操作串行。
 * @note 尚未 Start 时只提交 PLANNED；运行实例暂停期间拒绝提交。普通业务拒绝只返回结果，由引擎公开入口记录；
 * 分配、投递、推进或唤醒登记等致命错误直接终止。
 */
extern DOMINO_CODE dominoHumanExecutionSubmitSleep(domino_human_id_t human_id, domino_building_id_t building_id, uint8_t channel_index,
                                                   domino_task_id_t* task_id_out_ptr);

#endif
