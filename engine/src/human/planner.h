#ifndef DOMINO_ENGINE_HUMAN_PLANNER_H
#define DOMINO_ENGINE_HUMAN_PLANNER_H

#include "domino_shared_queue.h"
#include "message.h"

/** @brief 建立可扩容规划请求队列并绑定结果队列，不创建线程、不读取或修改世界。
 * @pre 引擎和 human 模块串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 * @pre result_queue_ptr 为执行器已初始化的 DominoThreadMessage 收件队列，保持有效直到规划资源销毁。
 * @note 不重复检查调用方已建立的队列和生命周期前置条件；资源初始化故障直接打印 stderr 并 exit。
 */
void dominoHumanPlannerInit(DominoThreadQueue* result_queue_ptr);

/** @brief 使用 Init 已准备的资源创建规划线程；线程内没有额外初始化，不设置单独的就绪握手。
 * @pre Init 已完成，业务时间冻结，引擎处于 STARTING；本次实例尚未启动。
 */
void dominoHumanPlannerStart(void);

/** @brief 时间状态或退出请求发布后唤醒批次边界等待；不等待线程回执。 */
void dominoHumanPlannerNotifyTimeChanged(void);

/** @brief 非阻塞复制并提交值快照；仅成功入队后请求成立。
 * @pre request_ptr 非空，人物 ID 与单调 request_id 已由执行器确定；此内部入口不重复校验。
 * @pre 执行线程已共同结算当前需求及任务，确认主通道无非终态任务、没有已有睡眠、容量未满且精力已到睡眠阈值。
 * @note Submit 仅由执行线程调用，每人最多一个有效请求；请求直接进入 FIFO 队列，按需扩容至公共队列上限。
 * 队列满或基础设施故障直接打印 stderr 并 exit。
 * request_id 由执行线程单调分配，不能复用消息 ID。
 * 冻结与全局退出请求不关闭入口，允许执行线程完成当前批次的提交；Stop 须在执行线程退出后调用。
 */
void dominoHumanPlannerSubmit(const DominoHumanPlanRequest* request_ptr);

/** @brief join 规划线程，保留队列及留队请求；停止故障直接打印 stderr 并 exit。
 * @pre 线程已创建，引擎已进入 STOPPING 并唤醒规划线程，且执行线程已 join。
 * @note 规划线程直接读取 dominoEngineIsStopping()；本接口不释放资源、不发布退出或重复唤醒。
 * 仅 Init、未 Start 时由引擎跳过本接口。
 */
void dominoHumanPlannerStop(void);

/** @brief 直接销毁只含值快照的请求队列并复位实例，不停止或 join 线程。
 * @pre 资源已 Init；执行与规划线程均未启动或已 join，不再提交请求，顺序由引擎和 human 模块保证。
 */
void dominoHumanPlannerDestroy(void);

#endif
