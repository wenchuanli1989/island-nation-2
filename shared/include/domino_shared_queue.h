#ifndef DOMINO_SHARED_QUEUE_H
#define DOMINO_SHARED_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include "domino_shared_error_codes.h"

#define DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY 20000U
#define DOMINO_THREAD_QUEUE_MAX_CAPACITY 100000U

/** @file domino_shared_queue.h
 * @brief 线程间收件箱：多生产者多消费者 FIFO，按值复制定长消息，满时按需扩容到上限（宪法第三章）。
 * @note 消息前 8 字节为 uint64_t 队列 ID，每次成功入队回写新 ID（从 1 递增，本次 Init 内唯一）。队列不解释消息内容，
 * 也不管理指针所指载荷。控制状态的等待协议为 Snapshot → 检查退出、冻结和工作 → Wait。
 * 所有接口的同步失败、分配失败和 ID 耗尽均致命退出。
 */

/** @brief 队列对象；调用方提供零初始化存储，不得改写控制字段，Init/Destroy 期间须排除其他操作。 */
typedef struct DominoThreadQueue {
    mtx_t mutex;
    cnd_t not_empty;  ///< 阻塞消费者和 Wait 等待者共用。
    cnd_t not_full;
    uint64_t last_id;     ///< 最后分配的消息 ID，由 mutex 保护。
    uint64_t wake_epoch;  ///< 入队成功、WakeAll 或 BeginFlush 时递增，只表示通知发生过。
    size_t item_size;
    void* items_ptr;
    uint32_t waiter_count;           ///< 正在条件等待的操作数，用于跳过无人接收的通知。
    uint32_t producer_waiter_count;  ///< 其中等待空位的生产者数。
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t capacity;
    uint32_t max_capacity;
    bool flushing_flag;  ///< 已进入排空状态：拒绝入队，剩余消息仍可消费。
    bool initialized;
} DominoThreadQueue;

/** @brief 分配初始容量的槽位并初始化同步对象。
 * @param item_size 单条消息字节数，至少 8（含开头的 ID）。
 * @note 参数无效、容量乘法溢出或重复 Init 致命退出。Destroy 后可再次 Init。
 */
void dominoThreadQueueInit(DominoThreadQueue* queue_ptr, size_t item_size, uint32_t initial_capacity, uint32_t max_capacity);

/** @brief 取得唤醒版本；须在检查控制状态和待处理工作之前调用。
 * @return 空指针 ERR_NULL_POINTER；未初始化 ERR_NOT_INITIALIZED。
 */
DOMINO_CODE dominoThreadQueueSnapshot(DominoThreadQueue* queue_ptr, uint64_t* epoch_out_ptr);

/** @brief 在队列锁内比较版本：已变化立即返回，否则等待入队、WakeAll 或 BeginFlush；不消费消息。
 * @param observed_epoch Snapshot 取得的版本。
 * @return CODE_OK 版本已变化，调用方重新检查状态和工作；已排空返回 ERR_QUEUE_FLUSHING（不要求队列已空）；
 *         空指针 ERR_NULL_POINTER；未初始化 ERR_NOT_INITIALIZED。
 * @note 只等版本变化、不以非空为条件，因此冻结时留队的消息不会让线程空转。不得持有通知方需要的锁调用。
 */
DOMINO_CODE dominoThreadQueueWait(DominoThreadQueue* queue_ptr, uint64_t observed_epoch);

/** @brief 进入排空状态（不可撤销）：推进版本并唤醒全部等待者；之后入队返回 ERR_QUEUE_FLUSHING，剩余消息仍可取出。
 * @note 空指针或未初始化时无操作。不消费消息、不等待线程退出。
 */
void dominoThreadQueueBeginFlush(DominoThreadQueue* queue_ptr);

/** @brief 推进版本并唤醒全部等待者，不改变数据或排空状态；空指针或未初始化时无操作。
 * @note 用于发布退出、解冻等控制状态后叫醒 Wait 等待者；阻塞在空队列上的消费者仍继续等待。
 */
void dominoThreadQueueWakeAll(DominoThreadQueue* queue_ptr);

/** @brief 丢弃剩余消息，释放存储与同步对象并清零，不排空、唤醒或 join。
 * @pre 全部使用者已结束并 join；剩余消息的载荷已由业务释放。仍有等待者时致命退出。
 */
void dominoThreadQueueDestroy(DominoThreadQueue* queue_ptr);

/** @brief 在锁内生成 ID 回写到消息首字段，再按值复制入队；满载时先扩容，已到上限则按 blocking 等待或返回。
 * @param item_ptr 可写消息，调用期间由当前线程独占。
 * @return CODE_OK 成功，可读取回写的 ID；失败不修改消息、不消耗 ID：空指针 ERR_NULL_POINTER，未初始化 ERR_NOT_INITIALIZED，
 *         已排空 ERR_QUEUE_FLUSHING，非阻塞且已满 ERR_QUEUE_FULL。
 * @note 阻塞等待时不得持有消费者处理消息所需的锁。
 */
DOMINO_CODE dominoThreadQueueProduce(DominoThreadQueue* queue_ptr, void* item_ptr, bool blocking);

/** @brief 按 FIFO 取出一条消息。
 * @param blocking 队列为空且未排空时是否等待。
 * @return 空指针 ERR_NULL_POINTER；未初始化 ERR_NOT_INITIALIZED；已排空且取空 ERR_QUEUE_FLUSHING；非阻塞且为空 ERR_QUEUE_EMPTY。
 */
DOMINO_CODE dominoThreadQueueConsume(DominoThreadQueue* queue_ptr, void* item_out_ptr, bool blocking);

/** @brief 一次持锁按 FIFO 取出当前可用的一批消息，不等待凑满。
 * @param items_out_ptr 至少容纳 max_count 条消息。
 * @param consumed_count_out_ptr 实际取出数，失败时为 0。
 * @return 返回值约定同 Consume；max_count 为 0 时 ERR_INVALID_PARAM。
 */
DOMINO_CODE dominoThreadQueueConsumeBatch(DominoThreadQueue* queue_ptr, void* items_out_ptr, uint32_t max_count, uint32_t* consumed_count_out_ptr,
                                          bool blocking);

#endif
