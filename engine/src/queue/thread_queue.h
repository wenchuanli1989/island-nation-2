#ifndef DOMINO_ENGINE_THREAD_QUEUE_H
#define DOMINO_ENGINE_THREAD_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>
#include <time.h>

#include "domino_shared_error_codes.h"
#include "thread_message.h"

#define DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY 100U
#define DOMINO_ENGINE_QUEUE_MAX_CAPACITY 10000U

/** @brief 队列端点；关停和恢复时须显式指定。 */
typedef enum DominoThreadQueueEnd {
    DOMINO_THREAD_QUEUE_BOTH = 0,
    DOMINO_THREAD_QUEUE_PRODUCER,
    DOMINO_THREAD_QUEUE_CONSUMER,
} DominoThreadQueueEnd;

/** @brief 可扩容的多生产者、多消费者 FIFO；消息前 8 字节固定为队列 ID，字段由队列内部维护。 */
typedef struct DominoThreadQueue {
    mtx_t mutex;
    cnd_t not_empty;
    uint64_t next_id;     ///< 成功入队时递增，由 mutex 保护。
    uint64_t wake_epoch;  ///< 入队成功、WakeAll 或 StopAndWake 时递增，仅作为唤醒版本，由 mutex 保护。
    size_t item_size;
    void* items_ptr;
    uint32_t waiter_count;  ///< 全部已登记的条件等待操作数，用于跳过无接收方的通知；不能替代调用方 join。
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t capacity;
    uint32_t max_capacity;
    atomic_bool lifecycle_stopping_flag;  ///< 生命周期关停后使等待和排空的同步错误可上报；保留至成功销毁。
    bool producer_stopped;
    bool consumer_stopped;
    bool initialized;
} DominoThreadQueue;

/**
 * @brief 初始化环形队列，item_size 必须包含开头的 uint64_t ID，且至少为 8 字节。
 * @note 跨模块收件队列使用 sizeof(DominoThreadMessage)，消息按 items_type 分派，同一队列允许混合载荷类型。
 * @return `CODE_OK` 成功；队列指针为空返回 `ERR_NULL_POINTER`；容量、元素大小无效返回 `ERR_INVALID_PARAM`；容量乘法溢出返回 `ERR_MEMORY_OVERFLOW`；
 *         内存分配失败返回 `ERR_MEMORY_ALLOC`；互斥锁初始化失败返回 `ERR_MUTEX_INIT`；条件变量初始化失败返回 `ERR_CONDITION_INIT`。
 * @note 不得对已初始化的队列重复调用；初始化须由调用方排除并发访问，成功销毁后可重新初始化。
 *       失败保留已分配资源，不回滚或清理；调用方须上报，由宿主决定何时结束进程；此前保持资源有效，不得重试或销毁半初始化队列。
 */
DOMINO_CODE dominoThreadQueueInit(DominoThreadQueue* queue_ptr, size_t item_size, uint32_t initial_capacity, uint32_t max_capacity);

/**
 * @brief 采样本队列的唤醒版本；须在检查模块控制状态和待处理消息之前调用。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；未初始化返回 ERR_NOT_INITIALIZED。
 * @note 版本仅用于与 Wait 比较是否变化，不是消息数、消息 ID 或业务时间；出队及 Stop/Resume 不推进版本。
 */
DOMINO_CODE dominoThreadQueueSnapshot(DominoThreadQueue* queue_ptr, uint64_t* epoch_out_ptr);

/**
 * @brief 在队列锁内复查唤醒版本，等待成功入队、WakeAll、端点关停或截止点，不消费消息。
 * @param observed_epoch 检查模块控制状态和消息之前取得的版本；版本已变化时立即返回。
 * @param deadline_utc_ptr TIME_UTC 绝对截止点；nullptr 表示无限等待。不是业务累计毫秒。
 * @return CODE_OK 表示版本已变化或到达截止点，调用方须重新检查控制状态、消息和业务到期条件；
 *         消费端已关停返回 ERR_QUEUE_CONSUMER_STOPPED，优先于生产端；生产端已关停返回 ERR_QUEUE_PRODUCER_STOPPED；
 *         空指针返回 ERR_NULL_POINTER；未初始化返回 ERR_NOT_INITIALIZED；tv_nsec 无效返回 ERR_INVALID_PARAM；
 *         StopAndWake 开始关闭握手后，同步失败返回 ERR_MUTEX_LOCK、ERR_MUTEX_UNLOCK 或 ERR_CONDITION_WAIT。
 * @note 仅等待版本变化，不以队列非空为返回条件，因此暂停时保留消息仍可休眠。生产端关停时，剩余消息仍可用 Consume 排空。
 *       控制状态由模块通过原子或自己的锁保护，先发布状态再 WakeAll。阻塞中的关停仍须 Stop → WakeAll。
 *       不得持队列锁或通知方需要的业务锁调用；销毁队列前须确保所有等待及其他队列操作均已结束。
 *       关闭握手中的同步失败仅执行必要解锁，由调用方上报，宿主决定何时结束进程；此前保持资源有效。
 *       普通运行和 Freeze/Thaw 的同步失败行为不变。
 */
DOMINO_CODE dominoThreadQueueWait(DominoThreadQueue* queue_ptr, uint64_t observed_epoch, const struct timespec* deadline_utc_ptr);

/**
 * @brief 仅设置指定端的停止标志，限制后续入队或出队；不通知或等待阻塞调用，重复关停会累积生效。
 * @param queue_ptr 待关停队列；空指针或未初始化时不操作。
 * @param end 显式选择 `DOMINO_THREAD_QUEUE_PRODUCER`、`DOMINO_THREAD_QUEUE_CONSUMER` 或 `DOMINO_THREAD_QUEUE_BOTH`；无效值不操作。
 * @note 仅关停生产端时，消费端可排空已有元素，随后返回 `ERR_QUEUE_PRODUCER_STOPPED`。
 *       仅关停消费端时，禁止出队，生产仍可入队和扩容，满载后返回 `ERR_QUEUE_CONSUMER_STOPPED`，不再等待。
 *       双端关停禁止入队和出队，未消费元素保留至恢复或销毁。
 *       等待者重新获得 mutex 后按当时的端点状态执行；快速关停再恢复时，旧调用可能继续等待或完成操作。
 *       已阻塞的消费者仍等待数据；需要其检查停止状态时，调用方须显式调用 `dominoThreadQueueWakeAll()`。
 */
void dominoThreadQueueStop(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end);

/**
 * @brief 生命周期关停：一次持锁设置指定端的停止标志、推进唤醒版本并通知全部等待者。
 * @param queue_ptr 待关停队列；空指针或未初始化时不操作。
 * @param end 显式选择生产端、消费端或双端；无效值不操作。
 * @return CODE_OK 成功或无需操作；同步失败返回 ERR_MUTEX_LOCK、ERR_MUTEX_UNLOCK 或 ERR_CONDITION_BROADCAST。
 * @note 端点和排空语义与 Stop 一致；成功后仍须由调用方 join 工作线程，再调用 Destroy。
 *       成功取得锁后发布生命周期关停标记，使 Wait 和 Consume 的同步错误可以返回；Resume 不撤销该标记。
 *       首次失败后仅执行必要解锁，不回滚停止状态、不继续通知、不清理资源，由调用方上报，宿主决定何时结束进程；此前保持资源有效。
 */
DOMINO_CODE dominoThreadQueueStopAndWake(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end);

/**
 * @brief 查询生命周期关停标记，不获取队列锁。
 * @note queue_ptr 须指向已初始化且未销毁的队列，不得与 Destroy 并发。
 */
bool dominoThreadQueueIsLifecycleStopping(const DominoThreadQueue* queue_ptr);

/**
 * @brief 恢复指定端，保留队列已有元素、顺序和容量；重复恢复不改变其他状态。
 * @param queue_ptr 待恢复队列；空指针或未初始化时不操作。
 * @param end 显式选择 `DOMINO_THREAD_QUEUE_PRODUCER`、`DOMINO_THREAD_QUEUE_CONSUMER` 或 `DOMINO_THREAD_QUEUE_BOTH`；无效值不操作。
 * @note 只在 mutex 保护下清除选中端的停止标志，另一端保持原状；不通知或等待阻塞调用。
 *       尚未观察到停止状态的等待者可继续执行；已返回关停错误的操作须由调用方重新发起。
 *       不会重新启动已经退出的工作线程，也不能与销毁并发。
 */
void dominoThreadQueueResume(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end);

/**
 * @brief 查询指定端是否显式关停；BOTH 要求两端均已关停。
 * @return 空指针或未初始化视为已关停；无效端点返回 false。
 */
bool dominoThreadQueueIsStopped(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end);

/**
 * @brief 显式通知全部消费者和版本等待者重新检查状态，不改变端点标志、数据或容量，也不等待其退出。
 * @param queue_ptr 待通知队列；空指针或未初始化时不操作。
 * @note 推进本队列唤醒版本，使 Wait 返回以检查模块控制状态，不占用队列容量。
 *       使用 Snapshot → 检查模块状态 → Wait 的线程可先发布退出标志，再 WakeAll、join，保留队列供其他线程的末批投递。
 *       阻塞出队若仍缺少数据且未停止，会继续等待；其生命周期退出须用 StopAndWake 关停端点并唤醒。
 *       唤醒本身不代表业务操作完成；所有使用者结束后才能排空并销毁队列。
 */
void dominoThreadQueueWakeAll(DominoThreadQueue* queue_ptr);

/**
 * @brief 在全部使用者停止访问后丢弃剩余元素并释放队列资源，不执行关停、唤醒或等待。
 * @return CODE_OK 成功；空指针或未初始化时不操作并返回 CODE_OK；存在已登记等待者返回 ERR_GAME_STATE_INVALID；同步失败返回 ERR_MUTEX_LOCK 或
 * ERR_MUTEX_UNLOCK。
 * @note 调用方须先完成所属模块停止协议，确保所有生产者不再访问，并成功 join 负责消费或等待的工作线程。
 *       Wait 等待者可用模块退出标志与 WakeAll 退出；阻塞出队等待者须用 StopAndWake 关停并唤醒。
 *       未启用线程的队列可直接销毁。
 *       销毁期间不得存在任何其他队列操作，包括等待 mutex 的调用；等待者计数检查不能替代调用方保证无并发访问。
 *       本函数不释放元素指向的业务载荷，剩余载荷须由模块事先排空并释放。
 *       失败后仅执行必要解锁，不继续销毁或清理资源，由调用方上报，宿主决定何时结束进程；此前保持资源有效。
 */
DOMINO_CODE dominoThreadQueueDestroy(DominoThreadQueue* queue_ptr);

/**
 * @brief 在队列锁内生成 ID，回写消息前 8 字节，再按值复制整条消息入队；满载时按需扩容，不等待消费空位。
 * @param item_ptr 可写消息，首字段必须为 uint64_t id；调用期间由当前线程独占。
 * @return CODE_OK 成功，调用方可读取自己的消息 ID；失败不修改消息，也不消耗 ID。
 *         空指针返回 ERR_NULL_POINTER；未初始化返回 ERR_NOT_INITIALIZED；生产端停止返回 ERR_QUEUE_PRODUCER_STOPPED；
 *         消费端停止且满载返回 ERR_QUEUE_CONSUMER_STOPPED；达到最大容量且满载返回 ERR_QUEUE_FULL；
 *         分配失败返回 ERR_MEMORY_ALLOC；ID 耗尽返回 ERR_ID_EXHAUSTED。
 * @note 每次成功入队都会覆盖原 ID，从 1 开始按入队顺序递增，仅在同一队列的一次 Init 生命周期内唯一。
 *       扩容及 Stop/Resume 保留计数，Destroy 后重新 Init 才重置；计数耗尽不回绕。
 *       成功入队在同一锁内推进唤醒版本，通知阻塞消费者及所有 Wait 等待者；失败不推进版本。
 *       非阻塞仅指不等待队列空位，仍需取得 mutex；队列只返回错误，由所属模块决定错误传播和实例终结。
 */
DOMINO_CODE dominoThreadQueueProduce(DominoThreadQueue* queue_ptr, void* item_ptr);

/**
 * @brief 按 FIFO 顺序复制并移除元素；双端均关停时优先返回消费端关停错误。
 * @param blocking 队列为空时，true 等待元素，false 立即返回；两种模式均需获取 mutex。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；未初始化返回 `ERR_NOT_INITIALIZED`；
 *         消费端已关停返回 `ERR_QUEUE_CONSUMER_STOPPED`；生产端已关停且队列已空返回 `ERR_QUEUE_PRODUCER_STOPPED`；
 *         非阻塞且队列为空返回 `ERR_QUEUE_EMPTY`。
 * @note 生命周期关停后的同步失败返回 ERR_MUTEX_LOCK、ERR_MUTEX_UNLOCK 或 ERR_CONDITION_WAIT；元素可能已经移出并复制，
 *       调用方仅上报失败，不继续处理或清理载荷。
 */
DOMINO_CODE dominoThreadQueueConsume(DominoThreadQueue* queue_ptr, void* item_out_ptr, bool blocking);

/**
 * @brief 在一次持锁操作中按 FIFO 顺序复制并移除当前可用的一批元素，不等待凑满批次。
 * @param items_out_ptr 输出数组，须能容纳 max_count 个 item_size 字节的元素。
 * @param max_count 单次最多取出的元素数，必须大于 0。
 * @param consumed_count_out_ptr 实际取出的元素数；成功时为 1..max_count，普通失败时为 0（指针非空时）。
 * @param blocking 仅当队列为空时，true 等待至少一个元素，false 立即返回；两种模式均需获取 mutex。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；max_count 为 0 返回 `ERR_INVALID_PARAM`；
 *         未初始化返回 `ERR_NOT_INITIALIZED`；消费端已关停返回 `ERR_QUEUE_CONSUMER_STOPPED`；
 *         生产端已关停且队列已空返回 `ERR_QUEUE_PRODUCER_STOPPED`；非阻塞且队列为空返回 `ERR_QUEUE_EMPTY`。
 *         生命周期关停后的同步失败返回 ERR_MUTEX_LOCK、ERR_MUTEX_UNLOCK 或 ERR_CONDITION_WAIT；
 *         失败时可能已经移出元素，输出数组和实际数量保留已完成的结果。
 * @note 消费端关停优先于已有数据和生产端关停；仅关停生产端时仍成功返回剩余批次，排空后的下一次调用才返回关停错误。
 *       普通失败不修改输出数组；生命周期同步失败只上报，不继续处理或清理本批载荷。
 *       生命周期退出用 StopAndWake 关停端点并唤醒阻塞等待；出队不需要通知生产者。
 */
DOMINO_CODE dominoThreadQueueConsumeBatch(DominoThreadQueue* queue_ptr, void* items_out_ptr, uint32_t max_count, uint32_t* consumed_count_out_ptr,
                                          bool blocking);

#endif
