#ifndef DOMINO_ENGINE_THREAD_QUEUE_H
#define DOMINO_ENGINE_THREAD_QUEUE_H

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
    cnd_t not_full;
    cnd_t waiters_drained;
    uint64_t next_id;     ///< 成功入队时递增，由 mutex 保护。
    uint64_t wake_epoch;  ///< 入队成功或 WakeAll 时递增，仅作为唤醒版本，由 mutex 保护。
    size_t item_size;
    void* items_ptr;
    uint32_t waiter_count;  ///< 全部已登记的条件等待操作数，仅销毁时等待其归零。
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t capacity;
    uint32_t max_capacity;
    bool producer_stopped;
    bool consumer_stopped;
    bool initialized;
} DominoThreadQueue;

/**
 * @brief 初始化环形队列，item_size 必须包含开头的 uint64_t ID，且至少为 8 字节。
 * @note 跨模块收件队列使用 sizeof(DominoThreadMessage)，消息按 items_type 分派，同一队列允许混合载荷类型。
 * @return `CODE_OK` 成功；队列指针为空返回 `ERR_NULL_POINTER`；容量、元素大小无效或容量乘法溢出返回 `ERR_INVALID_PARAM`；
 *         内存分配失败返回 `ERR_MEMORY_ALLOC`。
 * @note 不得对已初始化的队列重复调用；初始化须由调用方排除并发访问，销毁后可重新初始化。
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
 *         空指针返回 ERR_NULL_POINTER；未初始化返回 ERR_NOT_INITIALIZED；tv_nsec 无效返回 ERR_INVALID_PARAM。
 * @note 仅等待版本变化，不以队列非空为返回条件，因此暂停时保留消息仍可休眠。生产端关停时，剩余消息仍可用 Consume 排空。
 *       控制状态由模块通过原子或自己的锁保护，先发布状态再 WakeAll。阻塞中的关停仍须 Stop → WakeAll。
 *       不得持队列锁或通知方需要的业务锁调用；本函数与阻塞出入队的等待者均受 Destroy 的等待登记保护。
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
 *       已阻塞的调用仍等待数据或空位变化；需要其检查停止状态时，调用方须显式调用 `dominoThreadQueueWakeAll()`。
 */
void dominoThreadQueueStop(DominoThreadQueue* queue_ptr, DominoThreadQueueEnd end);

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
 * @brief 显式通知两端的所有条件等待者重新检查状态，不改变端点标志、数据或容量，也不等待其退出。
 * @param queue_ptr 待通知队列；空指针或未初始化时不操作。
 * @note 用于需要唤醒的退出流程：Stop → WakeAll → join → Destroy。
 *       推进本队列唤醒版本，使 Wait 返回以检查模块控制状态，不占用队列容量。
 *       阻塞出入队若仍缺少数据/空位且未停止，会继续等待；唤醒本身不代表业务操作完成。
 */
void dominoThreadQueueWakeAll(DominoThreadQueue* queue_ptr);

/**
 * @brief 关停双端并唤醒等待者，等待已登记的条件等待者退出，丢弃剩余元素并释放资源。
 * @note 调用方须阻止新操作，排除仍在等待 mutex 的调用及其他关停/恢复/唤醒/销毁操作；本函数不会 join 工作线程。
 *       常规流程仍须先 join 工作线程；本函数不释放元素指向的业务载荷，剩余载荷须由模块排空并释放。
 */
void dominoThreadQueueDestroy(DominoThreadQueue* queue_ptr);

/**
 * @brief 在队列锁内生成 ID，回写消息前 8 字节，再按值复制整条消息入队；满载时先尝试扩容。
 * @param item_ptr 可写消息，首字段必须为 uint64_t id；调用期间由当前线程独占。
 * @param blocking 达到最大容量时，true 等待空位，false 立即返回；两种模式均需获取 mutex。
 * @return CODE_OK 成功，调用方可读取自己的消息 ID；失败不修改消息，也不消耗 ID。
 *         空指针返回 ERR_NULL_POINTER；未初始化返回 ERR_NOT_INITIALIZED；生产端停止返回 ERR_QUEUE_PRODUCER_STOPPED；
 *         消费端停止且满载返回 ERR_QUEUE_CONSUMER_STOPPED；非阻塞满载返回 ERR_QUEUE_FULL；
 *         分配失败返回 ERR_MEMORY_ALLOC；ID 耗尽返回 ERR_OUT_OF_RANGE。
 * @note 每次成功入队都会覆盖原 ID，从 1 开始按入队顺序递增，仅在同一队列的一次 Init 生命周期内唯一。
 *       扩容及 Stop/Resume 保留计数，Destroy 后重新 Init 才重置；计数耗尽不回绕。
 *       成功入队在同一锁内推进唤醒版本，通知阻塞消费者及所有 Wait 等待者；失败不推进版本。
 */
DOMINO_CODE dominoThreadQueueProduce(DominoThreadQueue* queue_ptr, void* item_ptr, bool blocking);

/**
 * @brief 按 FIFO 顺序复制并移除元素；双端均关停时优先返回消费端关停错误。
 * @param blocking 队列为空时，true 等待元素，false 立即返回；两种模式均需获取 mutex。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；未初始化返回 `ERR_NOT_INITIALIZED`；
 *         消费端已关停返回 `ERR_QUEUE_CONSUMER_STOPPED`；生产端已关停且队列已空返回 `ERR_QUEUE_PRODUCER_STOPPED`；
 *         非阻塞且队列为空返回 `ERR_QUEUE_EMPTY`。
 */
DOMINO_CODE dominoThreadQueueConsume(DominoThreadQueue* queue_ptr, void* item_out_ptr, bool blocking);

/**
 * @brief 在一次持锁操作中按 FIFO 顺序复制并移除当前可用的一批元素，不等待凑满批次。
 * @param items_out_ptr 输出数组，须能容纳 max_count 个 item_size 字节的元素。
 * @param max_count 单次最多取出的元素数，必须大于 0。
 * @param consumed_count_out_ptr 实际取出的元素数；成功时为 1..max_count，失败时为 0（指针非空时）。
 * @param blocking 仅当队列为空时，true 等待至少一个元素，false 立即返回；两种模式均需获取 mutex。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；max_count 为 0 返回 `ERR_INVALID_PARAM`；
 *         未初始化返回 `ERR_NOT_INITIALIZED`；消费端已关停返回 `ERR_QUEUE_CONSUMER_STOPPED`；
 *         生产端已关停且队列已空返回 `ERR_QUEUE_PRODUCER_STOPPED`；非阻塞且队列为空返回 `ERR_QUEUE_EMPTY`。
 * @note 消费端关停优先于已有数据和生产端关停；仅关停生产端时仍成功返回剩余批次，排空后的下一次调用才返回关停错误。
 *       失败不修改输出数组。停止阻塞等待仍遵循 Stop → WakeAll；批量出队会通知等待空位的生产者。
 */
DOMINO_CODE dominoThreadQueueConsumeBatch(DominoThreadQueue* queue_ptr, void* items_out_ptr, uint32_t max_count, uint32_t* consumed_count_out_ptr,
                                          bool blocking);

#endif
