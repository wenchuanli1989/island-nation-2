#ifndef DOMINO_ENGINE_HUMAN_EXECUTOR_H
#define DOMINO_ENGINE_HUMAN_EXECUTOR_H

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"

/** @brief 初始化执行控制和本次运行的显式睡眠许可；不创建线程，失败保留已创建资源。 */
extern DOMINO_CODE dominoHumanExecutionInit(void);

/** @brief FinishStop 完成后释放执行控制与许可；未 Start 时也可直接退出。 */
extern void dominoHumanExecutionExit(void);

/** @brief 在冻结时刻校验已加载活动睡眠，创建执行／规划线程并等待启动就绪；首次运行分批建立调度。 */
extern DOMINO_CODE dominoHumanExecutionStart(void);

/** @brief 时钟或退出状态发布后唤醒执行／规划线程；不等待暂停回执，不重建已有调度。 */
extern void dominoHumanExecutionNotifyTimeChanged(void);

/** @brief 请求失败终结，唤醒线程及命令等待者；不等待退出，不释放实例资源。 */
extern void dominoHumanExecutionRequestFailure(DOMINO_CODE code);

/** @brief 发布退出请求并唤醒，等待执行线程完成当前批次，再停止并 join 规划线程。
 * @note timer 和收件队列仍存活；未 Start 时成功无操作，失败立即返回并保留剩余资源。
 */
extern DOMINO_CODE dominoHumanExecutionStop(void);

/** @brief 所有业务线程退出后统一结算到冻结时刻，释放留队消息、收件队列及唤醒关联。
 * @pre ExecutionStop 和 timer Exit 已成功；世界与睡眠许可仍存活，调用方独占人物写入权。
 */
extern DOMINO_CODE dominoHumanExecutionFinishStop(domino_runtime_ms_t now_ms);

/** @brief 显式授予同楼睡眠许可并允许后续自主睡眠；运行时由执行线程处理，许可不进入存档。
 * @pre 引擎公开入口已确认处于 INITIALIZED 或 RUNNING，且调用方为登记的客户端线程；与生命周期操作串行。
 * @note 自主睡眠仅在三槽为空、无任何非终态任务且有容量时选择主通道；高精力只登记下一精力阈值。
 */
extern DOMINO_CODE dominoHumanExecutionAuthorizeSleep(domino_human_id_t human_id, domino_building_id_t building_id);

/** @brief 提交并许可单行为睡眠；运行中完成当次人物推进后才返回成功。
 * @pre 引擎公开入口已确认处于 INITIALIZED 或 RUNNING，且调用方为登记的客户端线程；与生命周期操作串行。
 * @note 尚未 Run 时只提交 PLANNED；运行实例暂停期间拒绝提交。任务、许可分配或入队失败终结实例，不回滚已提交任务。
 *       当次推进或唤醒登记失败返回错误并终结实例，已成立的任务保留，不回滚。
 */
extern DOMINO_CODE dominoHumanExecutionSubmitSleep(domino_human_id_t human_id, domino_building_id_t building_id, uint8_t channel_index,
                                                   domino_task_id_t* task_id_out_ptr);

#endif
