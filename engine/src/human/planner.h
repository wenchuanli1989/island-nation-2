#ifndef DOMINO_ENGINE_HUMAN_PLANNER_H
#define DOMINO_ENGINE_HUMAN_PLANNER_H

#include "../queue/message.h"
#include "domino_shared_queue.h"

/** @brief 建立可扩容规划请求队列并绑定结果队列，不创建线程、不读取或修改世界。
 * @pre 引擎和 human 模块串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 * @pre result_queue_ptr 为执行器已初始化的 DominoHumanExecutionMessage 收件队列，保持有效直到规划资源销毁。
 * @note 不重复检查调用方已建立的队列和生命周期前置条件；资源初始化故障直接打印 stderr 并 exit。
 */
void dominoHumanPlannerInit(DominoThreadQueue* result_queue_ptr);

/** @brief 使用 Init 已准备的资源创建规划线程；线程内没有额外初始化，不设置单独的就绪握手。
 * @pre Init 已完成，业务时间冻结，引擎处于 STARTING；本次实例尚未启动。
 */
void dominoHumanPlannerStart(void);

/** @brief 时间状态或退出请求发布后唤醒批次边界等待；不等待线程回执。 */
void dominoHumanPlannerNotifyTimeChanged(void);

/** @brief 非阻塞提交人物身份，由私有请求队列分配 request_id；规划器消费时持 human 读锁查询业务数据。
 * @pre human_id 有效，人物没有有效规划请求；此内部入口不重复校验。
 * @pre 执行线程已共同结算当前需求及任务，确认主通道无非终态任务、没有已有睡眠、容量未满且精力已到睡眠阈值。
 * @pre 运行期调用方持 human 写锁，并在解锁前将返回值写入人物 active_plan_request_id；规划器的读锁保证身份发布后才读取。
 * @note 排队时的业务条件不视为读取时仍有效；规划器只读世界，执行器提交结果时重新检查当前业务条件。
 * 读取时人物不存在或 request_id 已失效则直接丢弃，不回传无效结果；运行期人物置异常时由执行器在同一写锁内使请求失效。
 * 身份仍匹配但精力不再满足触发条件时回传 STATE_CHANGED。
 * 解锁后身份仍可能失效，执行器消费结果时保留身份复核。
 * @note Submit 仅由引擎内部执行线程调用，不向客户端开放；每人最多一个有效请求，直接进入 FIFO 队列，按需扩容至公共队列上限。
 * 队列满或基础设施故障直接打印 stderr 并 exit。
 * request_id 使用本请求队列的消息 ID，从 1 单调分配且不回绕；耗尽由队列致命退出。
 * 结果消息的外层 ID 由结果收件队列另行分配，载荷保留本次请求 ID。
 * 冻结与全局退出请求不关闭入口，允许执行线程完成当前批次的提交；Stop 须在执行线程退出后调用。
 * @return 成功入队后生成的非零请求 ID，仅在本次 human 实例内有效，不持久化。
 */
uint64_t dominoHumanPlannerSubmit(domino_human_id_t human_id);

/** @brief join 规划线程，保留队列及留队请求；停止故障直接打印 stderr 并 exit。
 * @pre 线程已创建，引擎已进入 STOPPING 并唤醒规划线程，且执行线程已 join。
 * @note 规划线程直接读取 dominoEngineIsStopping()；本接口不释放资源、不发布退出或重复唤醒。
 * 仅 Init、未 Start 时由引擎跳过本接口。
 */
void dominoHumanPlannerStop(void);

/** @brief 直接销毁只含身份值的请求队列并复位实例，不停止或 join 线程。
 * @pre 资源已 Init；执行与规划线程均未启动或已 join，不再提交请求，顺序由引擎和 human 模块保证。
 */
void dominoHumanPlannerDestroy(void);

#endif
