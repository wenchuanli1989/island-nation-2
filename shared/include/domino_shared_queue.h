#ifndef DOMINO_SHARED_QUEUE_H
#define DOMINO_SHARED_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include "domino_shared_error_codes.h"

#define DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY 100U
#define DOMINO_THREAD_QUEUE_MAX_CAPACITY 10000U

/** @brief 可扩容的多生产者、多消费者 FIFO；消息前 8 字节固定为队列 ID，字段由队列内部维护。
 * @note 调用方提供零初始化存储，不得改写队列控制字段；Init/Destroy 期间须排除其他队列操作。
 */
typedef struct DominoThreadQueue {
    mtx_t mutex;
    cnd_t not_empty;
    cnd_t not_full;
    uint64_t next_id;     ///< 成功入队时递增，由 mutex 保护。
    uint64_t wake_epoch;  ///< 入队成功、WakeAll 或 BeginFlush 时递增，仅作为唤醒版本，由 mutex 保护。
    size_t item_size;     ///< 单个元素的字节数，Init 后保持不变。
    void* items_ptr;
    uint32_t waiter_count;           ///< 全部已登记的条件等待操作数，用于跳过无接收方的通知；不能替代调用方 join。
    uint32_t producer_waiter_count;  ///< 等待空位的生产者数量，由 mutex 保护；同时计入 waiter_count。
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t capacity;
    uint32_t max_capacity;
    bool flushing_flag;  ///< 进入排空状态后只允许消费；由 mutex 保护，首次零初始化或 Destroy 复位。
    bool initialized;    ///< Init 完整建立资源后置 true，Destroy 清零；供公共入口在加锁前检查。
} DominoThreadQueue;

/**
 * @brief 初始化环形队列，item_size 必须包含开头的 uint64_t ID，且至少为 8 字节。
 * @note 元素按 item_size 字节复制；除首字段 ID 外不解释内容，指针所指载荷的所有权由调用方管理。
 * @note 配置无效、容量乘法溢出、内存分配或同步对象初始化失败，直接打印 stderr 并 exit(EXIT_FAILURE)。
 * @note 首次对象须零初始化（静态存储、={0} 或 calloc）；已初始化队列重复 Init 直接打印 stderr 并 exit(EXIT_FAILURE)。
 *       初始化须由调用方排除并发访问；Init 完整建立内部资源，成功 Destroy 后可直接重新 Init。
 */
void dominoThreadQueueInit(DominoThreadQueue* queue_ptr, size_t item_size, uint32_t initial_capacity, uint32_t max_capacity);

/**
 * @brief 采样本队列的唤醒版本；须在检查模块控制状态和待处理消息之前调用。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；未初始化返回 ERR_NOT_INITIALIZED。
 * @note 版本仅用于与 Wait 比较是否变化，不是消息数、消息 ID 或业务时间；出队不推进版本。
 */
DOMINO_CODE dominoThreadQueueSnapshot(DominoThreadQueue* queue_ptr, uint64_t* epoch_out_ptr);

/**
 * @brief 在队列锁内复查唤醒版本，等待成功入队、WakeAll 或 BeginFlush，不消费消息。
 * @param observed_epoch 检查模块控制状态和消息之前取得的版本；版本已变化时立即返回。
 * @return CODE_OK 表示版本已变化，调用方须重新检查控制状态和待处理工作；
 *         已进入排空状态返回 ERR_QUEUE_FLUSHING，不要求队列已空；
 *         空指针返回 ERR_NULL_POINTER；未初始化返回 ERR_NOT_INITIALIZED。
 * @note 仅等待版本变化，不以队列非空为返回条件，因此暂停时保留消息仍可休眠。进入排空状态后，剩余消息仍可用 Consume 取出。
 *       控制状态由模块通过原子或自己的锁保护，先发布状态再 WakeAll。退出排空使用 BeginFlush 设置状态并唤醒。
 *       不得持队列锁或通知方需要的业务锁调用；销毁队列前须确保所有等待及其他队列操作均已结束。
 *       任何阶段的同步失败均直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
DOMINO_CODE dominoThreadQueueWait(DominoThreadQueue* queue_ptr, uint64_t observed_epoch);

/**
 * @brief 进入排空状态：一次持锁设置 flushing_flag、推进唤醒版本并通知全部等待者。
 * @param queue_ptr 待排空队列；空指针或未初始化时不操作。
 * @note 进入后拒绝所有入队，返回 ERR_QUEUE_FLUSHING；已有元素仍按 FIFO 消费，排空后的消费返回 ERR_QUEUE_FLUSHING，不再等待。
 *       Wait 立即返回 ERR_QUEUE_FLUSHING；已阻塞的生产者、消费者及版本等待者被唤醒后重新检查状态。
 *       重复调用保持排空状态并再次通知；不提供恢复，销毁后重新 Init 才可再次生产。
 *       本函数不消费元素或等待线程退出；调用方须清理剩余消息、确保全部使用者结束并完成 join，再调用 Destroy。
 *       同步失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
void dominoThreadQueueBeginFlush(DominoThreadQueue* queue_ptr);

/**
 * @brief 显式通知全部生产者、消费者和版本等待者重新检查状态，不改变排空状态、数据或容量，也不等待其退出。
 * @param queue_ptr 待通知队列；空指针或未初始化时不操作。
 * @note 推进本队列唤醒版本，使 Wait 返回以检查模块控制状态，不占用队列容量。
 *       使用 Snapshot → 检查模块状态 → Wait 的线程可先发布退出标志，再 WakeAll、join，保留队列供其他线程的末批投递。
 *       阻塞出队若仍缺少数据且未进入排空状态，会继续等待；其生命周期退出须用 BeginFlush 设置状态并唤醒。
 *       唤醒本身不代表业务操作完成；销毁前须确保全部使用者结束，并清理剩余消息的业务载荷。
 */
void dominoThreadQueueWakeAll(DominoThreadQueue* queue_ptr);

/**
 * @brief 在全部使用者停止访问后丢弃剩余元素并释放队列资源，不设置排空状态、唤醒或等待。
 * @note 空指针、未初始化、重复 Destroy、存在已登记等待者或同步失败时，直接打印 stderr 并 exit(EXIT_FAILURE)。
 * @note 调用方须先完成所属模块停止协议，确保所有生产者不再访问，并成功 join 负责消费或等待的工作线程。
 *       Wait 等待者可用模块退出标志与 WakeAll 退出；阻塞出队等待者须用 BeginFlush 进入排空状态并唤醒。
 *       未启用线程的队列可直接销毁。
 *       销毁期间不得存在任何其他队列操作，包括等待 mutex 的调用；等待者计数检查不能替代调用方保证无并发访问。
 *       本函数不释放元素指向的业务载荷，剩余载荷须由模块事先排空并释放。
 */
void dominoThreadQueueDestroy(DominoThreadQueue* queue_ptr);

/**
 * @brief 在队列锁内生成 ID，回写消息前 8 字节，再按值复制整条消息入队；满载时按需扩容，不等待消费空位。
 * @param item_ptr 可写消息，首字段必须为 uint64_t 类型的队列 ID，字段名不限；调用期间由当前线程独占。
 * @return CODE_OK 成功，调用方可读取自己的消息 ID；失败不修改消息，也不消耗 ID。
 *         空指针返回 ERR_NULL_POINTER；未初始化返回 ERR_NOT_INITIALIZED；已进入排空状态返回 ERR_QUEUE_FLUSHING；
 *         达到最大容量且满载返回 ERR_QUEUE_FULL。
 * @note 每次成功入队都会覆盖原 ID，从 1 开始按入队顺序递增，仅在同一队列的一次 Init 生命周期内唯一。
 *       扩容及 BeginFlush 保留计数，Destroy 后重新 Init 才重置；内存分配失败或计数耗尽直接打印 stderr 并 exit(EXIT_FAILURE)。
 *       成功入队在同一锁内推进唤醒版本，通知阻塞消费者及所有 Wait 等待者；失败不推进版本。
 *       非阻塞仅指不等待队列空位，仍需取得 mutex；容量、参数等错误交由所属模块处理，同步失败直接打印 stderr 并 exit。
 */
DOMINO_CODE dominoThreadQueueProduce(DominoThreadQueue* queue_ptr, void* item_ptr);

/**
 * @brief 阻塞入队；达到最大容量且满载时等待空位，唤醒后在同一队列锁内重新检查并入队。
 * @note ID 回写、扩容及所有权规则同 Produce；等待期间释放队列锁，消息存储须保持有效且不得被其他线程修改。
 *       消费释放空位后唤醒等待生产者；BeginFlush 也会唤醒它们，此后返回 ERR_QUEUE_FLUSHING。
 *       调用方不得持有消费者处理消息所需的锁。同步失败直接打印 stderr 并 exit(EXIT_FAILURE)。
 * @return 除满载时等待、不返回 ERR_QUEUE_FULL 外，其余返回值同 Produce。
 */
DOMINO_CODE dominoThreadQueueProduceBlocking(DominoThreadQueue* queue_ptr, void* item_ptr);

/**
 * @brief 按 FIFO 顺序复制并移除元素；进入排空状态后仍可消费已有元素。
 * @param blocking 正常状态下队列为空时，true 等待元素，false 立即返回；两种模式均需获取 mutex。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；未初始化返回 `ERR_NOT_INITIALIZED`；
 *         已进入排空状态且队列已空返回 `ERR_QUEUE_FLUSHING`；正常状态下非阻塞且队列为空返回 `ERR_QUEUE_EMPTY`。
 * @note 任何阶段的同步失败均直接打印 stderr 并 exit(EXIT_FAILURE)。
 */
DOMINO_CODE dominoThreadQueueConsume(DominoThreadQueue* queue_ptr, void* item_out_ptr, bool blocking);

/**
 * @brief 在一次持锁操作中按 FIFO 顺序复制并移除当前可用的一批元素，不等待凑满批次。
 * @param items_out_ptr 输出数组，须能容纳 max_count 个 item_size 字节的元素。
 * @param max_count 单次最多取出的元素数，必须大于 0。
 * @param consumed_count_out_ptr 实际取出的元素数；成功时为 1..max_count，失败时为 0（指针非空时）。
 * @param blocking 正常状态下队列为空时，true 等待至少一个元素，false 立即返回；两种模式均需获取 mutex。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；max_count 为 0 返回 `ERR_INVALID_PARAM`；
 *         未初始化返回 `ERR_NOT_INITIALIZED`；已进入排空状态且队列已空返回 `ERR_QUEUE_FLUSHING`；
 *         正常状态下非阻塞且队列为空返回 `ERR_QUEUE_EMPTY`。
 * @note 进入排空状态后仍成功返回剩余批次，排空后的下一次调用才返回 ERR_QUEUE_FLUSHING，不再等待新消息。
 *       失败不修改输出数组；任何阶段的同步失败均直接打印 stderr 并 exit(EXIT_FAILURE)。
 *       生命周期退出用 BeginFlush 进入排空状态并唤醒阻塞等待；出队后通知等待空位的生产者。
 */
DOMINO_CODE dominoThreadQueueConsumeBatch(DominoThreadQueue* queue_ptr, void* items_out_ptr, uint32_t max_count, uint32_t* consumed_count_out_ptr,
                                          bool blocking);

#endif
