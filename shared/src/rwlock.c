#include <string.h>

#include "domino_shared_common.h"
#include "domino_shared_rwlock.h"

/** @brief 公共操作共享的参数检查；Init/Destroy 由调用方排除并发，initialized 在使用期间保持不变。 */
static inline void requireInitialized(const DominoRWLock* lock_ptr) {
    if (lock_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "rwlock requires an instance");
    }
    if (!lock_ptr->initialized) {
        DOMINO_EXIT_WITH_ERROR(ERR_NOT_INITIALIZED, "rwlock requires initialization");
    }
}

void dominoRWLockInit(DominoRWLock* lock_ptr) {
    if (lock_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "rwlock initialization requires an instance");
    }
    if (lock_ptr->initialized) {
        DOMINO_EXIT_WITH_ERROR(ERR_ALREADY_INITIALIZED, "rwlock is already initialized");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_init(&lock_ptr->mutex, mtx_plain), ERR_MUTEX_INIT, "rwlock:mtx_init");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&lock_ptr->readers_ready), ERR_CONDITION_INIT, "rwlock:cnd_init(readers)");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&lock_ptr->writers_ready), ERR_CONDITION_INIT, "rwlock:cnd_init(writers)");
    lock_ptr->initialized = true;
}

void dominoRWLockDestroy(DominoRWLock* lock_ptr) {
    requireInitialized(lock_ptr);
    if (lock_ptr->writer_active || lock_ptr->reader_count != 0U || lock_ptr->waiting_reader_count != 0U || lock_ptr->waiting_writer_count != 0U) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "rwlock destroy requires all users to finish");
    }
    cnd_destroy(&lock_ptr->readers_ready);
    cnd_destroy(&lock_ptr->writers_ready);
    mtx_destroy(&lock_ptr->mutex);
    memset(lock_ptr, 0, sizeof(*lock_ptr));
}

void dominoRWLockReadLock(DominoRWLock* lock_ptr) {
    requireInitialized(lock_ptr);
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&lock_ptr->mutex), ERR_MUTEX_LOCK, "rwlock:read_lock");
    if (lock_ptr->waiting_reader_count == UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "rwlock waiting reader count overflow");
    }
    lock_ptr->waiting_reader_count++;
    while (lock_ptr->writer_active || lock_ptr->waiting_writer_count != 0U) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&lock_ptr->readers_ready, &lock_ptr->mutex), ERR_CONDITION_WAIT, "rwlock:wait_reader");
    }
    lock_ptr->waiting_reader_count--;
    if (lock_ptr->reader_count == UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "rwlock reader count overflow");
    }
    lock_ptr->reader_count++;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&lock_ptr->mutex), ERR_MUTEX_UNLOCK, "rwlock:read_lock");
}

void dominoRWLockReadUnlock(DominoRWLock* lock_ptr) {
    requireInitialized(lock_ptr);
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&lock_ptr->mutex), ERR_MUTEX_LOCK, "rwlock:read_unlock");
    if (lock_ptr->reader_count == 0U) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "rwlock read unlock requires a held read lock");
    }
    lock_ptr->reader_count--;
    if (lock_ptr->reader_count == 0U && lock_ptr->waiting_writer_count != 0U) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_signal(&lock_ptr->writers_ready), ERR_CONDITION_SIGNAL, "rwlock:signal_writer");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&lock_ptr->mutex), ERR_MUTEX_UNLOCK, "rwlock:read_unlock");
}

void dominoRWLockWriteLock(DominoRWLock* lock_ptr) {
    requireInitialized(lock_ptr);
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&lock_ptr->mutex), ERR_MUTEX_LOCK, "rwlock:write_lock");
    if (lock_ptr->waiting_writer_count == UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "rwlock waiting writer count overflow");
    }
    lock_ptr->waiting_writer_count++;
    while (lock_ptr->writer_active || lock_ptr->reader_count != 0U) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_wait(&lock_ptr->writers_ready, &lock_ptr->mutex), ERR_CONDITION_WAIT, "rwlock:wait_writer");
    }
    lock_ptr->waiting_writer_count--;
    lock_ptr->writer_active = true;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&lock_ptr->mutex), ERR_MUTEX_UNLOCK, "rwlock:write_lock");
}

void dominoRWLockWriteUnlock(DominoRWLock* lock_ptr) {
    requireInitialized(lock_ptr);
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&lock_ptr->mutex), ERR_MUTEX_LOCK, "rwlock:write_unlock");
    if (!lock_ptr->writer_active) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "rwlock write unlock requires a held write lock");
    }
    lock_ptr->writer_active = false;
    if (lock_ptr->waiting_writer_count != 0U) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_signal(&lock_ptr->writers_ready), ERR_CONDITION_SIGNAL, "rwlock:signal_writer");
    } else if (lock_ptr->waiting_reader_count != 0U) {
        DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&lock_ptr->readers_ready), ERR_CONDITION_BROADCAST, "rwlock:broadcast_readers");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&lock_ptr->mutex), ERR_MUTEX_UNLOCK, "rwlock:write_unlock");
}
