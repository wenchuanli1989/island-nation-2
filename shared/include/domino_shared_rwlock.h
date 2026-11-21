#ifndef DOMINO_SHARED_RWLOCK_H
#define DOMINO_SHARED_RWLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

/** @brief 基于标准线程接口的读写锁：多个读者共享，写者独占。
 * @note 有写者等待时新读者也等待，避免写者饥饿；不保证 FIFO，不支持递归、升级或降级。
 * 首次对象须零初始化，Init 后不得复制或移动；解锁须由加锁线程调用对应接口。
 * 所有接口在空指针、未初始化、状态不符、计数溢出或同步失败时致命退出。
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

/** @brief 准备同步资源；须排除并发访问，Destroy 后可再次 Init。 */
void dominoRWLockInit(DominoRWLock* lock_ptr);

/** @brief 释放同步资源并清零对象，不等待持锁者或唤醒线程。
 * @pre 调用方已结束全部访问；仍有持锁者或等待者时致命退出，但计数检查不能代替调用方的线程回收。
 */
void dominoRWLockDestroy(DominoRWLock* lock_ptr);

/** @brief 取得共享读锁；有写者执行或等待时阻塞。 */
void dominoRWLockReadLock(DominoRWLock* lock_ptr);

/** @brief 释放一次读锁；最后一个读者唤醒等待的写者。 */
void dominoRWLockReadUnlock(DominoRWLock* lock_ptr);

/** @brief 取得独占写锁，等待全部读者和前一个写者退出。 */
void dominoRWLockWriteLock(DominoRWLock* lock_ptr);

/** @brief 释放写锁；优先唤醒等待的写者，否则唤醒全部读者。 */
void dominoRWLockWriteUnlock(DominoRWLock* lock_ptr);

#endif
