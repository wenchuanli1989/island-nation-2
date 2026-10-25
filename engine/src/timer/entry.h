#ifndef DOMINO_ENGINE_TIMER_H
#define DOMINO_ENGINE_TIMER_H

#include "domino_shared_error_codes.h"
#include "message.h"

/**
 * @brief 初始化命令队列、空计时堆和同步资源，不创建线程；生命周期函数由所属控制线程串行调用。
 * @pre 引擎生命周期处于 INITIALIZING。
 * @pre 首次使用或上一轮已完成 Destroy，模块已为空状态。
 * @note 不创建、注册或管理业务事件队列，每条登记请求直接携带自己的队列；堆容量按运行请求量增长。
 * @note 调用顺序由引擎保证，不维护模块生命周期状态；资源创建失败直接打印到 stderr 并 exit。
 */
void dominoTimerModuleInit(void);

/**
 * @brief 使用 Init 已准备的资源创建计时线程。
 * @pre 引擎生命周期处于 STARTING，timer 已初始化且未 Start/Stop，time 保持冻结。
 * @note 消费者准备好后由引擎解冻并通知，线程在批次边界检查冻结，无需冻结回执。
 * @note 线程创建失败直接打印到 stderr 并 exit。
 */
void dominoTimerModuleStart(void);

/**
 * @brief join 计时线程，保留命令队列、计时堆和同步资源。
 * @pre 已 Start，引擎已进入 STOPPING 并唤醒计时线程；由引擎保证只调用一次。
 * @note 调用前须结束所有其他 timer API 调用；业务事件队列须保持有效且生产端可用，直到本接口返回。
 * @note 每轮先检查退出再检查冻结；已开始的一批堆处理及命令迁移完整执行，下一轮和睡眠前重新检查退出。
 * @note 线程直接读取 dominoEngineIsStopping()，不维护独立退出状态；正在结束的业务批次仍可登记及取消。
 * @note 本接口不释放资源、不发布退出或重复唤醒；仅 Init 时引擎不调用本接口。
 * @note 不重复校验调用顺序；线程回收失败打印到 stderr 并 exit。
 */
void dominoTimerModuleStop(void);

/**
 * @brief 释放命令队列、计时堆和同步资源并清空整个模块状态，不停止或 join 线程。
 * @pre 已初始化；创建过的线程已由 Stop join，并已结束所有其他 timer API 调用。
 * @note 丢弃尚未投递的请求；已投递事件及业务队列由业务方处理，不在本接口中移除或销毁。
 * @note 仅 Init 的实例也可由引擎直接调用本接口；调用顺序由引擎保证，资源销毁失败打印到 stderr 并 exit。
 */
void dominoTimerModuleDestroy(void);

/** @brief 时间状态变更或引擎退出请求发布后唤醒计时线程；不改变 time，不等待线程回执或业务消费结束。
 * @pre 引擎已完成 timer 初始化，且尚未销毁同步资源。
 */
void dominoTimerNotifyTimeChanged(void);

/**
 * @brief 按现实毫秒延迟提交未取消的计时请求；入队后通知计时线程，不等待该请求处理完成。
 * @pre 引擎已准备 timer 和目标事件队列，并保证调用期间资源有效；不重复校验初始化状态。
 * @note 调用方填写 delay_ms、event_queue_ptr、event_type 和 cancelled_flag；输入的 timer_id、due_ms 不参与登记。
 * @note 事件队列必须已初始化并存储 DominoThreadMessage，可混合 timer、AI、命令等类型；计时线程停止及业务消费结束前不得销毁或重建。
 * @note 到期消息 type 为 DOMINO_THREAD_MESSAGE_TIMER，payload.timer 按值保存 DominoTimer，无需分配或释放载荷。
 *       消费方检查类型后读取 payload.timer 并跳过取消项；消息 id 与 timer_id 独立。
 * @note 校验通过后写入请求的 due_ms；队列生成并写入首字段 timer_id，再按值复制入队；排队时间计入延迟。
 * @note 请求记录须在调用期间由当前线程独占；每次调用先将 timer_id 置为 0，成功入队后写回非零登记 ID。
 * @note 校验失败时 timer_id 为 0，due_ms 及其余输入字段保持不变；基础设施错误直接终止进程。
 * @note 延迟为 0 仍经队列异步投递；消费和业务处理完全由队列所属业务方负责。
 * @note 请求已标记取消时直接返回 ERR_INVALID_PARAM，timer_id 为 0，不入队也不消耗消息 ID。
 * @return 请求实际入队时返回 CODE_OK 并写回 timer_id；请求或队列指针为空返回 ERR_NULL_POINTER；
 *         已取消请求或队列元素大小不匹配返回 ERR_INVALID_PARAM；delay_ms 与当前时刻之和超过 UINT32_MAX 毫秒返回 ERR_OUT_OF_RANGE。
 *         非空请求被拒绝时 timer_id 为 0，不提交命令；业务结果不在 timer 内打印，由调用边界按语义决定是否记录。
 * @note 入队成功后取得 request_mutex 记录唤醒通知，可能等待命令批次、堆操作或取消扫描；无需等待该请求入堆即可按 ID 取消。
 * @note 命令提交、后台入堆或到期事件投递失败均打印到 stderr 并 exit；队列满、停止、内存不足及 ID 耗尽不重试。
 *       业务时钟采样失败或累计时间超出 32 位毫秒范围同样直接终止进程，不使用越界时间或回绕值。
 * @note time 冻结或引擎退出请求发布后，正在结束的业务批次仍允许登记和取消；生命周期控制方须保持 timer 资源有效。
 *       计时线程只在批次边界检查退出和冻结；一批含最多 64 个堆项及 64 条命令，普通冻结或退出不打断已开始的批次。
 *       解冻并通知后继续处理保留的请求；取消命令不入堆，取消堆顶直接回收。
 */
DOMINO_CODE dominoTimerSchedule(DominoTimer* timer_ptr);

/**
 * @brief 按精确的累计未冻结现实毫秒截止点异步登记，不通过相对延迟重新计算截止点。
 * @param timer_ptr 调用方独占的请求，填写 event_queue_ptr、event_type 和未取消标记；原 timer_id、due_ms、delay_ms 不作登记依据。
 * @param due_ms 32 位绝对截止点，0 与 UINT32_MAX 均合法；已到期时保留原值并尽快异步投递。
 * @return 请求校验结果与 Schedule 相同；当前业务时刻越界或提交失败直接报告并终止进程。
 * @note 校验通过后写入 due_ms，delay_ms 写为尚未到期时的 due_ms - now_ms，否则为 0；不使用 now_ms + delay_ms 重算截止点。
 * @note 每次调用先清零非空请求的 timer_id；校验失败时 due_ms、delay_ms 不变。
 * @note 成功入队后写回非零 timer_id；队列、载荷所有权、冻结、取消、生命周期和后台失败合同均与 Schedule 相同。
 */
DOMINO_CODE dominoTimerScheduleAt(DominoTimer* timer_ptr, domino_runtime_ms_t due_ms);

/**
 * @brief 按登记 ID 查找命令和堆项，再到指定业务队列查找到期事件并设置取消标志，不删除命令或事件。
 * @pre 引擎已准备 timer 和目标事件队列，并保证调用期间资源有效；不重复校验初始化状态。
 * @param event_queue_ptr 用于查找已投递事件的业务队列，应传登记请求中填写的队列，必须已初始化且存储 DominoThreadMessage。
 * @note 登记 ID 在本次 Init 生命周期内唯一，命令和堆项只按 ID 匹配；传入的业务队列仅限定已投递事件的查找范围。
 * @return 找到请求并设置取消标志返回 CODE_OK，已取消但仍可找到的请求也返回 CODE_OK；
 *         ID 为 0 或队列元素大小不匹配返回 ERR_INVALID_PARAM；队列指针为空返回 ERR_NULL_POINTER；
 *         未知、已移除或已出队的请求返回 ERR_NOT_FOUND。
 * @note 业务结果不在 timer 内打印，由调用边界按语义决定是否记录。
 * @note 同步失败属于致命错误，直接打印到 stderr 并 exit。
 * @note 已投递事件的取消与业务 Consume 在队列锁内竞争；业务必须在出队后检查 cancelled_flag 并跳过取消事件。
 *       已出队的事件不能撤回，业务自行校验对象存在、任务状态及当前 timer_id。
 * @note 不提交命令或消耗消息 ID；调用方须排除与 Init/Stop/Destroy 并发。
 * @note 查找及标记与请求转移由 request_mutex 串行；业务消费只使用队列锁，不需持有 timer 锁。
 * @note ID 仅在本次 Init 生命周期内有效；复用业务队列进入下一次 timer Init 前须排空旧事件。
 */
DOMINO_CODE dominoTimerCancel(domino_timer_id_t timer_id, DominoThreadQueue* event_queue_ptr);

#endif
