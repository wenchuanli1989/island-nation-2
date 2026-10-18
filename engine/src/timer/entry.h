#ifndef DOMINO_ENGINE_TIMER_H
#define DOMINO_ENGINE_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "../queue/thread_queue.h"
#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

/** @brief 64 位登记 ID，不是时间值；业务毫秒统一使用 32 位 domino_runtime_ms_t。 */
typedef uint64_t domino_timer_id_t;

/** @brief 登记命令、堆项及到期消息的计时载荷；到期时封装为 DominoThreadMessage。 */
typedef struct DominoTimer {
    uint64_t id;                         ///< 登记命令队列的消息 ID；到期投递不修改载荷内部 ID。
    domino_timer_id_t timer_id;          ///< 登记命令的消息 ID，在本次 Init 生命周期内作为稳定取消凭据。
    DominoThreadQueue* event_queue_ptr;  ///< 业务方拥有的通用收件队列，元素类型为 DominoThreadMessage。
    domino_runtime_ms_t due_ms;          ///< 32 位累计未冻结现实毫秒截止点，用于内部排序和到期判断。
    domino_runtime_ms_t delay_ms;        ///< Schedule 的相对延迟；ScheduleAt 写入登记时剩余延迟，0 表示已到期。
    uint8_t event_type;
    bool cancelled_flag;  ///< 取消标志，正常登记时初始化为 false；登记接口拒绝已标记取消的请求。
} DominoTimer;

static_assert(sizeof(DominoTimer) == 40, "DominoTimer size must be 40 bytes");

/**
 * @brief 初始化命令队列和同步资源后启动计时线程；生命周期函数由所属控制线程串行调用。
 * @note 调用前须完成 time 初始化；不创建、注册或管理业务事件队列，每条登记请求直接携带自己的队列。
 * @note time 冻结时线程在批次边界等待；消费者准备好后解冻并通知，无需冻结回执。
 * @note 资源初始化失败直接返回当前错误，不回滚或清理已取得资源；上层收到错误后结束进程，不得重试或调用 Exit 清理。
 * @return 成功返回 CODE_OK；已初始化返回 ERR_ALREADY_INITIALIZED；资源初始化失败返回相应错误码。
 */
DOMINO_CODE dominoTimerModuleInit(void);

/**
 * @brief 通知计时线程退出并 join，释放命令队列、计时堆和同步资源。
 * @note 调用前须结束所有其他 timer API 调用；业务事件队列须保持有效且生产端可用，直到本接口返回。
 * @note 每轮先检查退出再检查冻结；已开始的一批堆处理及命令迁移完整执行，下一轮和睡眠前重新检查退出。
 * @note 引擎可提前发布全局退出并唤醒线程；正在结束的业务批次仍可登记及取消，须在本接口前结束这些调用。
 * @note 尚未投递的请求随资源释放而丢弃；已投递的事件由业务方处理或清理，不在本接口中移除。
 * @note 业务事件队列由业务方停止、排空及销毁；未初始化或已经退出调用无操作并返回 CODE_OK。
 * @note 线程先记录退出结果再发布完成标志；本接口确认完成且结果成功后才 join 和释放资源。
 * @note 本接口自身同步步骤、线程退出结果、线程回收或队列销毁失败立即返回当前错误，不继续清理；上层收到错误后结束进程，不得重试。
 *       计时线程自身同步原语失效沿用原有终止策略，包括其退出阶段，不带锁发布退出回执。
 * @return 成功返回 CODE_OK；失败返回本次退出操作或线程退出结果的错误码。
 */
DOMINO_CODE dominoTimerModuleExit(void);

/** @brief 时间状态变更或引擎退出请求发布后唤醒计时线程；不改变 time，不等待线程回执或业务消费结束。 */
void dominoTimerNotifyTimeChanged(void);

/**
 * @brief 实例失败时记录首错并唤醒计时线程，不 join 或销毁资源。
 * @note 不与 Init/Exit 并发；初始化未完成、已停止或已经请求失败时调用无操作，不访问已销毁或不可信的同步对象。
 * @param code 终结实例的负错误码。
 */
void dominoTimerRequestFailure(DOMINO_CODE code);

/**
 * @brief 按现实毫秒延迟提交未取消的计时请求；入队后通知计时线程，不等待该请求处理完成。
 * @note 调用方填写 delay_ms、event_queue_ptr、event_type 和 cancelled_flag；输入的 id、timer_id、due_ms 不参与登记。
 * @note 事件队列必须已初始化并存储 DominoThreadMessage，可混合 timer、AI、命令等类型；计时线程停止及业务消费结束前不得销毁或重建。
 * @note 到期消息 items_type 为 DOMINO_THREAD_MESSAGE_TIMER，items_ptr 指向独立 DominoTimer 副本，items_size 为 sizeof(DominoTimer)。
 *       消费方校验类型和大小后读取载荷，处理或跳过取消项后均须调用 dominoThreadMessageRelease；消息 id 与 timer_id 独立。
 * @note 校验通过后写入请求的 due_ms，再由队列按值复制；排队时间计入延迟，成功入队时回写 id 并令 timer_id = id。
 * @note 请求记录须在调用期间由当前线程独占；每次调用先将 timer_id 置为 0，成功入队后写回非零登记 ID。
 * @note 校验失败时 id 和 due_ms 不变；入队失败时 id 不变、已计算的 due_ms 保留；其余输入字段保持不变。
 * @note 延迟为 0 仍经队列异步投递；消费和业务处理完全由队列所属业务方负责。
 * @note 请求已标记取消时直接返回 ERR_INVALID_PARAM，timer_id 为 0，不入队也不消耗消息 ID。
 * @return 请求实际入队时返回 CODE_OK 并写回 timer_id；请求或队列指针为空返回 ERR_NULL_POINTER；模块或队列未初始化返回 ERR_NOT_INITIALIZED；
 *         已取消请求或队列元素大小不匹配返回 ERR_INVALID_PARAM；当前时间或绝对到期时间超过 UINT32_MAX 毫秒返回 ERR_OUT_OF_RANGE。
 *         命令队列满或内存不足等提交错误返回并终结实例；非空请求失败时 timer_id 为 0，不提交命令。
 * @note 入队成功后取得 request_mutex 记录唤醒通知，可能等待命令批次、堆操作或取消扫描；无需等待该请求入堆即可按 ID 取消。
 * @note 后台入堆或到期事件投递失败时直接发布实例首错，计时线程停止推进并发布退出回执，不等待诊断输出。
 *       目标队列达到最大容量、内存分配失败及消息 ID 耗尽等投递错误均不重试。
 *       后台读取累计时间超出 32 位毫秒范围时同样终结实例，不继续使用越界时间或回绕值；失败后资源保活供宿主结束进程。
 * @note time 冻结或引擎退出请求发布后，正在结束的业务批次仍允许登记和取消；生命周期控制方须保持 timer 资源有效。
 *       计时线程只在批次边界检查退出和冻结；一批含最多 64 个堆项及 64 条命令，普通冻结或退出不打断已开始的批次。
 *       解冻并通知后继续处理保留的请求；取消命令不入堆，取消堆顶直接回收，实例故障仍立即停止当前批次。
 */
DOMINO_CODE dominoTimerSchedule(DominoTimer* timer_ptr);

/**
 * @brief 按精确的累计未冻结现实毫秒截止点异步登记，不通过相对延迟重新计算截止点。
 * @param timer_ptr 调用方独占的请求，填写 event_queue_ptr、event_type 和未取消标记；原 id、timer_id、due_ms、delay_ms 不作登记依据。
 * @param due_ms 32 位绝对截止点，0 与 UINT32_MAX 均合法；已到期时保留原值并尽快异步投递。
 * @return 校验和提交错误与 Schedule 相同；仍检查当前业务时刻范围，不因截止点合法而接受越界时钟。
 * @note 校验通过后写入 due_ms，delay_ms 写为尚未到期时的 due_ms - now_ms，否则为 0；不使用 now_ms + delay_ms 重算截止点。
 * @note 每次调用先清零非空请求的 timer_id；校验失败时 id、due_ms、delay_ms 不变，入队失败时保留已写入的 due_ms 和 delay_ms。
 * @note 成功入队后写回非零 timer_id；队列、载荷所有权、冻结、取消、生命周期和后台失败合同均与 Schedule 相同。
 */
DOMINO_CODE dominoTimerScheduleAt(DominoTimer* timer_ptr, domino_runtime_ms_t due_ms);

/**
 * @brief 按 ID 和业务队列查找命令、堆项或已投递事件并设置取消标志，不删除命令或事件。
 * @param event_queue_ptr 登记请求中填写的同一通用收件队列，必须已初始化且存储 DominoThreadMessage。
 * @return 成功返回 CODE_OK；ID 为 0 或队列元素大小不匹配返回 ERR_INVALID_PARAM；队列指针为空返回 ERR_NULL_POINTER；
 *         模块或队列未初始化返回 ERR_NOT_INITIALIZED；未知、已取消或已出队的请求返回 ERR_NOT_FOUND；畸形 timer 消息返回 ERR_INVALID_MESSAGE。
 * @note 已投递事件的取消与业务 Consume 在队列锁内竞争；业务必须在出队后检查 cancelled_flag 并跳过取消事件。
 *       已出队的事件不能撤回，业务自行校验对象存在、任务状态及当前 timer_id。
 * @note 不提交命令或消耗消息 ID；调用方须排除与 Init/Exit 并发。
 * @note 查找及标记与请求转移由 request_mutex 串行；业务消费只使用队列锁，不需持有 timer 锁。
 * @note ID 仅在本次 Init 生命周期内有效；复用业务队列进入下一次 timer Init 前须排空旧事件。
 */
DOMINO_CODE dominoTimerCancel(domino_timer_id_t timer_id, DominoThreadQueue* event_queue_ptr);

#endif
