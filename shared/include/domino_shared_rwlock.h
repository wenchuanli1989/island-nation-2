#ifndef DOMINO_SHARED_RWLOCK_H
#define DOMINO_SHARED_RWLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

/** @brief 基于标准线程接口的通用读写锁：允许多个读者，写者独占访问。
 * @note 存在等待写者时，新读者等待，避免持续到来的读者饿死写者；不保证线程间 FIFO 或读者无饥饿。
 * 首次对象须零初始化，Init 后不得复制、移动或修改内部成员；地址保持有效至 Destroy 返回。
 * 不支持递归加锁、读写升级或降级；必须由取得锁的线程以对应接口解锁，不跟踪各读者的线程身份。
 * 本结构只管理同步，不解释数据、线程角色或所属模块生命周期。
 */
typedef struct DominoRWLock {
    mtx_t mutex;
    cnd_t readers_ready;
    cnd_t writers_ready;
    uint32_t reader_count;
    uint32_t waiting_reader_count;
    uint32_t waiting_writer_count;
    bool writer_active;
    bool initialized;
} DominoRWLock;

/** @brief 准备同步资源；须排除任何并发访问，首次对象为零状态，Destroy 后可再次 Init。
 * @note 空指针、重复初始化或同步资源失败直接致命退出。
 */
void dominoRWLockInit(DominoRWLock* lock_ptr);

/** @brief 释放同步资源并复位对象，不等待持锁者、唤醒线程或 join。
 * @pre 调用方已结束全部访问，包括正在等待锁的操作；计数检查不能替代调用方的线程回收保证。
 * @note 空指针、未初始化或存在持锁者、等待者时直接致命退出。
 */
void dominoRWLockDestroy(DominoRWLock* lock_ptr);

/** @brief 等待取得共享读锁；已有写者执行或等待时进入条件等待。
 * @note 空指针、未初始化、计数溢出或同步失败直接致命退出；不得持有写者继续工作所需的锁。
 */
void dominoRWLockReadLock(DominoRWLock* lock_ptr);

/** @brief 释放当前线程的一次共享读锁；最后一个读者唤醒等待写者。
 * @note 空指针、未初始化、没有读锁或同步失败直接致命退出。
 */
void dominoRWLockReadUnlock(DominoRWLock* lock_ptr);

/** @brief 等待取得独占写锁，直到全部读者及前一个写者退出。
 * @note 空指针、未初始化、计数溢出或同步失败直接致命退出；不得持有其他使用者继续工作所需的锁。
 */
void dominoRWLockWriteLock(DominoRWLock* lock_ptr);

/** @brief 释放当前线程的独占写锁；优先唤醒等待写者，否则唤醒全部读者。
 * @note 空指针、未初始化、没有写锁或同步失败直接致命退出。
 */
void dominoRWLockWriteUnlock(DominoRWLock* lock_ptr);

#endif
