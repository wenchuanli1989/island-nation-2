#ifndef DOMINO_ENGINE_TIMER_H
#define DOMINO_ENGINE_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "../queue/thread_queue.h"
#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

/** @brief 64 位登记 ID，不是时间值；业务毫秒统一使用 32 位 domino_runtime_ms_t。 */
typedef uint64_t domino_timer_id_t;

/** @brief 登记命令、堆项及到期事件共用的按值计时记录；业务对象关联由消费方维护。 */
typedef struct DominoTimer {
    uint64_t id;                         ///< 当前队列的消息 ID，每次入队由队列覆盖。
    domino_timer_id_t timer_id;          ///< 登记命令的消息 ID，在本次 Init 生命周期内作为稳定取消凭据。
    DominoThreadQueue* event_queue_ptr;  ///< 业务方拥有的到期事件队列，元素类型为 DominoTimer。
    domino_runtime_ms_t due_ms;          ///< 32 位累计未冻结现实毫秒截止点，用于内部排序和到期判断。
    domino_runtime_ms_t delay_ms;        ///< 32 位现实毫秒延迟，从 Schedule 调用时起计算；0 表示尽快投递。
    uint8_t event_type;
    bool cancelled_flag;  ///< 取消标志，正常登记时初始化为 false；Schedule 拒绝已标记取消的请求。
} DominoTimer;

static_assert(sizeof(DominoTimer) == 40, "DominoTimer size must be 40 bytes");

/**
 * @brief 初始化命令队列和同步资源后启动计时线程；生命周期函数由所属控制线程串行调用。
 * @note 不创建、注册或管理业务事件队列；每条登记请求直接携带自己的队列。
 * @return 成功返回 CODE_OK；已初始化返回 ERR_ALREADY_INITIALIZED；资源初始化失败返回相应错误码。
 */
DOMINO_CODE dominoTimerModuleInit(void);

/**
 * @brief 通知计时线程退出并 join，释放命令队列、计时堆和同步资源。
 * @note 调用前须结束所有其他 timer API 调用；业务事件队列须保持有效且生产端可用，直到本接口返回。
 * @note 已开始的一轮可能完成最多 64 个堆项和 64 条命令；线程在下一轮头部退出，睡眠前也检查停止标志。
 * @note 尚未投递的请求随资源释放而丢弃；已投递的事件由业务方处理或清理，不在本接口中移除。
 * @note 业务事件队列由业务方停止、排空及销毁；未初始化或已经退出时调用无操作。
 */
void dominoTimerModuleExit(void);

/** @brief time 初始化完成、冻结或解冻后通知线程重新检查累计现实时间；不改变 time 或业务消费状态。 */
void dominoTimerNotifyTimeChanged(void);

/**
 * @brief 按现实毫秒延迟提交未取消的计时请求；入队后通知计时线程，不等待该请求处理完成。
 * @note 调用方填写 delay_ms、event_queue_ptr、event_type 和 cancelled_flag；输入的 id、timer_id、due_ms 不参与登记。
 * @note 事件队列必须已初始化、只存储 DominoTimer，且在计时线程停止及业务消费结束前不得销毁或重新初始化。
 * @note 校验通过后写入请求的 due_ms，再由队列按值复制；排队时间计入延迟，成功入队时回写 id 并令 timer_id = id。
 * @note 请求记录须在调用期间由当前线程独占；每次调用先将 timer_id 置为 0，成功入队后写回非零登记 ID。
 * @note 校验失败时 id 和 due_ms 不变；入队失败时 id 不变、已计算的 due_ms 保留；其余输入字段保持不变。
 * @note 延迟为 0 仍经队列异步投递；消费和业务处理完全由队列所属业务方负责。
 * @note 请求已标记取消时直接返回 ERR_INVALID_PARAM，timer_id 为 0，不入队也不消耗消息 ID。
 * @return 请求实际入队时返回 CODE_OK 并写回 timer_id；请求或队列指针为空返回 ERR_NULL_POINTER；模块或队列未初始化返回 ERR_NOT_INITIALIZED；
 *         已取消请求或队列元素大小不匹配返回 ERR_INVALID_PARAM；当前时间或绝对到期时间超过 UINT32_MAX 毫秒返回 ERR_OUT_OF_RANGE。
 *         命令队列满或内存不足等提交错误直接返回；非空请求失败时 timer_id 为 0，不提交命令。
 * @note 入队成功后取得 request_mutex 记录唤醒通知，可能等待命令批次、堆操作或取消扫描；无需等待该请求入堆即可按 ID 取消。
 * @note 后台入堆或到期事件投递失败时，记录事件队列地址、timer ID 和错误码并调用 abort() 立即终止程序。
 *       目标队列达到最大容量、内存分配失败及消息 ID 耗尽等投递错误均不重试。
 *       后台读取累计时间超出 32 位毫秒范围时同样记录错误并终止，不继续使用越界时间或回绕值。
 * @note time 冻结期间仍允许登记和取消；计时线程仅在每轮头部检查冻结，发现冻结后只等待，解冻并通知后恢复处理。
 *       已开始的一轮可能完成；处理时取消命令不入堆，取消堆顶直接回收。
 */
DOMINO_CODE dominoTimerSchedule(DominoTimer* timer_ptr);

/**
 * @brief 按 ID 和业务队列查找命令、堆项或已投递事件并设置取消标志，不删除命令或事件。
 * @param event_queue_ptr 登记请求中填写的同一业务队列，必须已初始化且只存储 DominoTimer。
 * @return 成功返回 CODE_OK；ID 为 0 或队列元素大小不匹配返回 ERR_INVALID_PARAM；队列指针为空返回 ERR_NULL_POINTER；
 *         模块或队列未初始化返回 ERR_NOT_INITIALIZED；未知、已取消或已出队的请求返回 ERR_NOT_FOUND。
 * @note 已投递事件的取消与业务 Consume 在队列锁内竞争；业务必须在出队后检查 cancelled_flag 并跳过取消事件。
 *       已出队的事件不能撤回，业务自行校验对象存在、任务状态及当前 timer_id。
 * @note 不提交命令或消耗消息 ID；调用方须排除与 Init/Exit 并发。
 * @note 查找及标记与请求转移由 request_mutex 串行；业务消费只使用队列锁，不需持有 timer 锁。
 * @note ID 仅在本次 Init 生命周期内有效；复用业务队列进入下一次 timer Init 前须排空旧事件。
 */
DOMINO_CODE dominoTimerCancel(domino_timer_id_t timer_id, DominoThreadQueue* event_queue_ptr);

#endif
