#ifndef DOMINO_ENGINE_HUMAN_WAKEUP_H
#define DOMINO_ENGINE_HUMAN_WAKEUP_H

#include "../timer/entry.h"
#include "domino_shared_human.h"

/**
 * @brief 初始化人物唯一唤醒关联；收件队列须已初始化并存储 DominoThreadMessage。
 * @note 运行期间仅由 human 执行线程调用本模块；Init/Exit 与执行线程及人物容器生命周期串行。
 *       人物运行态须已重置；本接口不启动 timer，也不登记事件。
 */
DOMINO_CODE dominoHumanWakeupInit(DominoThreadQueue* event_queue_ptr);

/**
 * @brief 释放关联并清空人物唤醒运行字段；不取消或修改持久任务。
 * @note timer 已退出，人物及 data 容器仍有效；不再调用 Cancel。旧收件消息须在下一次 Init 前排空。
 */
void dominoHumanWakeupExit(void);

/**
 * @brief 为人物登记唯一绝对业务截止点；同一有效截止点保留原登记。
 * @note due_ms 原样交给 ScheduleAt，0、已到期时刻和 UINT32_MAX 均合法。
 *       替换前检查版本上限，随后撤销旧身份并提交 timer；关联按需插入，分配失败直接返回，不重试或回滚已提交命令。
 *       失败由执行器处理调度错误。
 * @pre human_ptr 为已查询且完成 data 绑定的人物；调用期间人物池不增删或重排。
 */
DOMINO_CODE dominoHumanWakeupSchedule(DominoHuman* human_ptr, domino_runtime_ms_t due_ms);

/**
 * @brief 撤销人物有效登记；无登记时成功，timer 的 ERR_NOT_FOUND 视为已撤销。
 * @note 有登记时先检查版本上限，再移除关联并清空人物 timer ID；其他取消错误正常返回，旧身份仍已失效。
 * @pre human_ptr 为已查询且完成 data 绑定的人物；调用期间人物池不增删或重排。
 */
DOMINO_CODE dominoHumanWakeupCancel(DominoHuman* human_ptr);

/**
 * @brief 消费有效唤醒身份并借用对应人物；本接口不推进人物业务，也不释放消息载荷。
 * @return 取消、缺失或过期身份返回 ERR_NOT_FOUND；仅成功时修改输出。
 * @note 取出对应关联，仅在人物当前 ID 和版本均匹配时清空其 timer ID；旧事件不能清除后来登记。
 *       调用前由收件方检查消息类型、载荷大小及指针，调用后统一释放消息；输出人物在人物池增删或重排后须重新查询。
 */
DOMINO_CODE dominoHumanWakeupConsume(const DominoTimer* timer_ptr, DominoHuman** human_out_ptr);

#endif
