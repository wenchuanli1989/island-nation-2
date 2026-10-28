#ifndef DOMINO_ENGINE_GAME_TIMER_H
#define DOMINO_ENGINE_GAME_TIMER_H

#include "domino_shared_error_codes.h"
#include "message.h"

/**
 * @brief 初始化命令队列、空计时堆和同步资源，不创建线程；生命周期函数由所属控制线程串行调用。
 * @pre 引擎生命周期处于 INITIALIZING。
 * @pre 首次使用或上一轮已完成 Destroy，模块已为空状态。
 * @note 不创建、注册或管理业务事件队列，每条登记请求直接携带自己的队列；堆容量按运行请求量增长。
 * @note 调用顺序由引擎保证，不维护模块生命周期状态；资源创建失败直接打印到 stderr 并 exit。
 */
void dominoGameTimerModuleInit(void);

/**
 * @brief 使用 Init 已准备的资源创建计时线程。
 * @pre 引擎生命周期处于 STARTING，game_timer 已初始化且未 Start/Stop，time 保持冻结。
 * @note 消费者准备好后由引擎解冻并通知，线程在批次边界检查冻结，无需冻结回执。
 * @note 线程创建失败直接打印到 stderr 并 exit。
 */
void dominoGameTimerModuleStart(void);

/**
 * @brief join 计时线程，保留命令队列、计时堆和同步资源。
 * @pre 已 Start，引擎已进入 STOPPING 并唤醒计时线程；由引擎保证只调用一次。
 * @note 调用前须结束所有其他 game_timer API 调用；业务事件队列须保持有效且生产端可用，直到本接口返回。
 * @note 每轮先检查退出再检查冻结；已开始的一批堆处理及命令迁移完整执行，下一轮和睡眠前重新检查退出。
 * @note 线程直接读取 dominoEngineIsStopping()，不维护独立退出状态；正在结束的业务批次仍可登记及取消。
 * @note 本接口不释放资源、不发布退出或重复唤醒；仅 Init 时引擎不调用本接口。
 * @note 不重复校验调用顺序；线程回收失败打印到 stderr 并 exit。
 */
void dominoGameTimerModuleStop(void);

/**
 * @brief 释放命令队列、计时堆和同步资源并清空整个模块状态，不停止或 join 线程。
 * @pre 已初始化；创建过的线程已由 Stop join，并已结束所有其他 game_timer API 调用。
 * @note 丢弃尚未投递的请求；已投递事件及业务队列由业务方处理，不在本接口中移除或销毁。
 * @note 仅 Init 的实例也可由引擎直接调用本接口；调用顺序由引擎保证，资源销毁失败打印到 stderr 并 exit。
 */
void dominoGameTimerModuleDestroy(void);

/** @brief 时间状态变更或引擎退出请求发布后唤醒计时线程；不改变 time，不等待线程回执或业务消费结束。
 * @pre 引擎已完成 game_timer 初始化，且尚未销毁同步资源。
 */
void dominoGameTimerNotifyTimeChanged(void);

/**
 * @brief 按精确的累计未冻结现实毫秒截止点异步登记；入队后通知计时线程，不等待请求处理完成。
 * @pre 引擎已准备 game_timer 和目标事件队列，并保证调用期间资源有效；不重复校验初始化状态。
 * @param event_queue_ptr 业务方拥有、已初始化且存储 DominoThreadMessage 的队列。
 * @param due_ms 32 位绝对截止点，0 与 UINT32_MAX 均合法；已到期时保留原值并尽快异步投递。
 * @param event_type 业务方定义的事件类型，game_timer 原样传递。
 * @param data_id 业务方定义的 64 位标识，game_timer 原样按值传递，不解释含义或持有业务对象。
 * @param game_timer_id_out_ptr 非空且由调用线程独占的登记 ID 输出；调用先清零，入队成功后写回非零 ID。
 * @return 请求入队成功返回 CODE_OK；队列或输出指针为空返回 ERR_NULL_POINTER；
 *         队列元素大小不匹配返回 ERR_INVALID_PARAM。参数错误不提交命令、不消耗 ID，非空输出保持为 0。
 * @note 在局部构造未取消记录，命令队列生成首字段 game_timer_id 并按值复制；截止点不通过相对延迟重新计算。
 *       仍采样并校验当前业务时刻，采样失败或超出 32 位毫秒范围直接打印到 stderr 并 exit。
 * @note 到期消息 type 为 DOMINO_THREAD_MESSAGE_GAME_TIMER，payload.game_timer 按值保存 DominoGameTimer，无需分配或释放载荷。
 *       消费方检查类型后读取 payload.game_timer 并跳过取消项；消息 id 与 game_timer_id 独立。
 * @note 事件队列可混合 game_timer、AI、命令等类型；计时线程停止及业务消费结束前不得销毁、搬移或重建。
 * @note 入队成功后取得 request_mutex 发出唤醒信号，可能等待命令批次、堆操作或取消扫描；无需等待入堆即可按 ID 取消。
 * @note 命令提交、后台入堆或到期事件投递失败均打印到 stderr 并 exit；队列满、停止、内存不足及 ID 耗尽不重试。
 *       参数错误由调用边界按语义决定是否记录，game_timer 内不重复打印。
 * @note time 冻结或引擎退出请求发布后，正在结束的业务批次仍允许登记和取消；生命周期控制方须保持 game_timer 资源有效。
 *       计时线程只在批次边界检查退出和冻结；一批含最多 64 个堆项及 64 条命令，普通冻结或退出不打断已开始的批次。
 *       解冻并通知后按原截止点继续处理保留请求；取消命令不入堆，取消堆顶直接回收。
 */
DOMINO_CODE dominoGameTimerScheduleAt(DominoThreadQueue* event_queue_ptr, domino_runtime_ms_t due_ms, uint8_t event_type, uint64_t data_id,
                                      domino_game_timer_id_t* game_timer_id_out_ptr);

/**
 * @brief 按登记 ID 查找命令和堆项，再到指定业务队列查找到期事件并设置取消标志，不删除命令或事件。
 * @pre 引擎已准备 game_timer 和目标事件队列，并保证调用期间资源有效；不重复校验初始化状态。
 * @param event_queue_ptr 用于查找已投递事件的业务队列，应传登记请求中填写的队列，必须已初始化且存储 DominoThreadMessage。
 * @note 登记 ID 在本次 Init 生命周期内唯一，命令和堆项只按 ID 匹配；传入的业务队列仅限定已投递事件的查找范围。
 * @return 找到请求并设置取消标志返回 CODE_OK，已取消但仍可找到的请求也返回 CODE_OK；
 *         ID 为 0 或队列元素大小不匹配返回 ERR_INVALID_PARAM；队列指针为空返回 ERR_NULL_POINTER；
 *         未知、已移除或已出队的请求返回 ERR_NOT_FOUND。
 * @note 业务结果不在 game_timer 内打印，由调用边界按语义决定是否记录。
 * @note 同步失败属于致命错误，直接打印到 stderr 并 exit。
 * @note 已投递事件的取消与业务 Consume 在队列锁内竞争；业务必须在出队后检查 cancelled_flag 并跳过取消事件。
 *       已出队的事件不能撤回，业务自行校验对象存在、任务状态及当前 game_timer_id。
 * @note 不提交命令或消耗消息 ID；调用方须排除与 Init/Stop/Destroy 并发。
 * @note 查找及标记与请求转移由 request_mutex 串行；业务消费只使用队列锁，不需持有 game_timer 锁。
 * @note ID 仅在本次 Init 生命周期内有效；复用业务队列进入下一次 game_timer Init 前须排空旧事件。
 */
DOMINO_CODE dominoGameTimerCancel(domino_game_timer_id_t game_timer_id, DominoThreadQueue* event_queue_ptr);

#endif
