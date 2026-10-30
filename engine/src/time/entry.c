#include "entry.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "domino_shared_common.h"

/** @brief 底层以 64 位纳秒保留采样精度；业务时刻和时长始终使用 domino_runtime_ms_t。 */
typedef struct DominoTimeSnapshot {
    uint64_t session_start_monotonic_ns;
    uint64_t runtime_datetime_base_ns;
    uint64_t freeze_epoch;
    bool frozen;
} DominoTimeSnapshot;

/** @brief 生命周期控制方唯一写入；读者仅接受 sequence 相同且为偶数的原子字段快照。 */
typedef struct DominoTimeCache {
    _Atomic uint64_t sequence;
    _Atomic uint64_t session_start_monotonic_ns;
    _Atomic uint64_t runtime_datetime_base_ns;
    _Atomic uint64_t freeze_epoch;
    _Atomic bool frozen;
} DominoTimeCache;

/** @brief 首次使用及 Destroy 后均为空的冻结快照；加载仅恢复累计基准，解冻时才建立单调起点。 */
static DominoTimeCache g_domino_time_cache = {.freeze_epoch = 1U, .frozen = true};

/** @brief 冻结状态切换、等待登记和通知共用 mutex；计数仅用于观察本轮冻结等待。 */
typedef struct DominoTimeWait {
    cnd_t condition;
    mtx_t mutex;
    uint32_t waiter_count;
} DominoTimeWait;

static DominoTimeWait g_domino_time_wait;

/** @brief 发布快照并关闭写入窗口；调用方已将 sequence 置为奇数，且持有唯一写入权。 */
static void publishTimeCache(const DominoTimeSnapshot* next_ptr) {
    atomic_store(&g_domino_time_cache.session_start_monotonic_ns, next_ptr->session_start_monotonic_ns);
    atomic_store(&g_domino_time_cache.runtime_datetime_base_ns, next_ptr->runtime_datetime_base_ns);
    atomic_store(&g_domino_time_cache.freeze_epoch, next_ptr->freeze_epoch);
    atomic_store(&g_domino_time_cache.frozen, next_ptr->frozen);
    atomic_fetch_add(&g_domino_time_cache.sequence, 1);
}

static void writeTimeCache(const DominoTimeSnapshot* next_ptr) {
    atomic_fetch_or(&g_domino_time_cache.sequence, 1);
    publishTimeCache(next_ptr);
}

/** @brief 仅加载原子字段；调用方负责通过 sequence 验证快照一致性。 */
static inline DominoTimeSnapshot loadTimeCacheFields(void) {
    return (DominoTimeSnapshot){
        .session_start_monotonic_ns = atomic_load(&g_domino_time_cache.session_start_monotonic_ns),
        .runtime_datetime_base_ns = atomic_load(&g_domino_time_cache.runtime_datetime_base_ns),
        .freeze_epoch = atomic_load(&g_domino_time_cache.freeze_epoch),
        .frozen = atomic_load(&g_domino_time_cache.frozen),
    };
}

/** @brief 读取一致的时间状态快照。 */
static inline DominoTimeSnapshot readTimeCache(void) {
    while (true) {
        uint64_t before = atomic_load(&g_domino_time_cache.sequence);
        if ((before & 1U) != 0U) {
            continue;
        }

        DominoTimeSnapshot snapshot = loadTimeCacheFields();
        uint64_t after = atomic_load(&g_domino_time_cache.sequence);
        if (before == after) {
            return snapshot;
        }
    }
}

uint64_t dominoGetRuntimeDateTimeBase(void) {
    return readTimeCache().runtime_datetime_base_ns;
}

void dominoSetRuntimeDateTimeBase(uint64_t base_ns) {
    DominoTimeSnapshot snapshot = readTimeCache();
    snapshot.runtime_datetime_base_ns = base_ns;
    writeTimeCache(&snapshot);
}

void dominoTimeModuleInit(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_init(&g_domino_time_wait.mutex, mtx_plain), ERR_MUTEX_INIT, "time:mtx_init");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&g_domino_time_wait.condition), ERR_CONDITION_INIT, "time:cnd_init");
}

void dominoTimeModuleDestroy(void) {
    cnd_destroy(&g_domino_time_wait.condition);
    mtx_destroy(&g_domino_time_wait.mutex);
    memset(&g_domino_time_wait, 0, sizeof(g_domino_time_wait));
    atomic_store_explicit(&g_domino_time_cache.session_start_monotonic_ns, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_time_cache.runtime_datetime_base_ns, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_time_cache.freeze_epoch, 1U, memory_order_relaxed);
    atomic_store_explicit(&g_domino_time_cache.frozen, true, memory_order_relaxed);
    atomic_store_explicit(&g_domino_time_cache.sequence, 0U, memory_order_relaxed);
}

/** @brief 计算累计纳秒；单调时钟倒退或累计值溢出时直接终止进程。 */
static uint64_t calculateRuntimeNs(const DominoTimeSnapshot* snapshot_ptr, uint64_t now_monotonic_ns) {
    if (now_monotonic_ns < snapshot_ptr->session_start_monotonic_ns) {
        DOMINO_EXIT_WITH_ERROR(ERR_CLOCK_READ, "单调时钟倒退: sampled_ns=%" PRIu64 ", start_ns=%" PRIu64, now_monotonic_ns,
                               snapshot_ptr->session_start_monotonic_ns);
    }
    const uint64_t elapsed_real_ns = now_monotonic_ns - snapshot_ptr->session_start_monotonic_ns;
    if (elapsed_real_ns > UINT64_MAX - snapshot_ptr->runtime_datetime_base_ns) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "累计纳秒溢出: base_ns=%" PRIu64 ", elapsed_ns=%" PRIu64, snapshot_ptr->runtime_datetime_base_ns,
                               elapsed_real_ns);
    }
    return snapshot_ptr->runtime_datetime_base_ns + elapsed_real_ns;
}

void dominoTimeModuleFreeze(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_time_wait.mutex), ERR_MUTEX_LOCK, "time freeze:mtx_lock");
    DominoTimeSnapshot snapshot = readTimeCache();
    if (!snapshot.frozen) {
        if (snapshot.freeze_epoch == UINT64_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "冻结轮次耗尽");
        }
        /* 先关闭读窗口再采样，避免读者接受比冻结基准更晚的业务时刻。
         * 致命错误直接 exit；退出回调不得重入引擎 API。 */
        atomic_fetch_or(&g_domino_time_cache.sequence, 1);
        uint64_t now_monotonic_ns = dominoMonotonicTimeNs();
        snapshot.runtime_datetime_base_ns = calculateRuntimeNs(&snapshot, now_monotonic_ns);
        snapshot.session_start_monotonic_ns = now_monotonic_ns;
        snapshot.freeze_epoch++;
        snapshot.frozen = true;
        g_domino_time_wait.waiter_count = 0U;
        publishTimeCache(&snapshot);
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_time_wait.mutex), ERR_MUTEX_UNLOCK, "time freeze:mtx_unlock");
}

void dominoTimeModuleThaw(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_time_wait.mutex), ERR_MUTEX_LOCK, "time thaw:mtx_lock");
    DominoTimeSnapshot snapshot = readTimeCache();
    if (!snapshot.frozen) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "解冻时间失败: 时钟未冻结");
    }

    snapshot.session_start_monotonic_ns = dominoMonotonicTimeNs();
    snapshot.frozen = false;
    writeTimeCache(&snapshot);
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&g_domino_time_wait.condition), ERR_CONDITION_BROADCAST, "time thaw:cnd_broadcast");
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_time_wait.mutex), ERR_MUTEX_UNLOCK, "time thaw:mtx_unlock");
}

__attribute__((hot)) bool dominoTimeModuleIsFrozen(void) {
    return readTimeCache().frozen;
}

__attribute__((hot)) uint64_t dominoTimeModuleGetFreezeEpoch(void) {
    const DominoTimeSnapshot snapshot = readTimeCache();
    return snapshot.frozen ? snapshot.freeze_epoch : 0U;
}

void dominoTimeModuleWait(uint64_t observed_epoch) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_time_wait.mutex), ERR_MUTEX_LOCK, "time wait:mtx_lock");
    for (;;) {
        const DominoTimeSnapshot snapshot = readTimeCache();
        if (dominoEngineIsStopping() || !snapshot.frozen || snapshot.freeze_epoch != observed_epoch) {
            break;
        }
        g_domino_time_wait.waiter_count++;
        const int result_code = cnd_wait(&g_domino_time_wait.condition, &g_domino_time_wait.mutex);
        /* 解冻后可能在本线程重新持锁前又冻结；旧轮返回不能扣减新轮显示计数。 */
        if (atomic_load(&g_domino_time_cache.freeze_epoch) == observed_epoch) {
            g_domino_time_wait.waiter_count--;
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(result_code, ERR_CONDITION_WAIT, "time wait:cnd_wait");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_time_wait.mutex), ERR_MUTEX_UNLOCK, "time wait:mtx_unlock");
}

void dominoTimeModuleWakeAll(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_time_wait.mutex), ERR_MUTEX_LOCK, "time wake:mtx_lock");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&g_domino_time_wait.condition), ERR_CONDITION_BROADCAST, "time wake:cnd_broadcast");
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_time_wait.mutex), ERR_MUTEX_UNLOCK, "time wake:mtx_unlock");
}

uint32_t dominoTimeModuleGetFreezeWaiterCount(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_domino_time_wait.mutex), ERR_MUTEX_LOCK, "time count:mtx_lock");
    const uint32_t waiter_count = g_domino_time_wait.waiter_count;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_domino_time_wait.mutex), ERR_MUTEX_UNLOCK, "time count:mtx_unlock");
    return waiter_count;
}

__attribute__((hot)) void dominoTimeModuleGetDateTimeNow(uint64_t* runtime_ns_ptr) {
    if (runtime_ns_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "读取累计纳秒失败: 输出指针为空");
    }
    while (true) {
        uint64_t before = atomic_load(&g_domino_time_cache.sequence);
        if ((before & 1U) != 0U) {
            continue;
        }

        DominoTimeSnapshot snapshot = loadTimeCacheFields();
        uint64_t monotonic_now_ns = 0;
        /* 时钟采样纳入版本校验，避免并发冻结后仍按旧状态计算更晚的时间。 */
        if (!snapshot.frozen) {
            monotonic_now_ns = dominoMonotonicTimeNs();
        }

        uint64_t after = atomic_load(&g_domino_time_cache.sequence);
        if (before != after) {
            continue;
        }
        if (snapshot.frozen) {
            *runtime_ns_ptr = snapshot.runtime_datetime_base_ns;
            return;
        }
        *runtime_ns_ptr = calculateRuntimeNs(&snapshot, monotonic_now_ns);
        return;
    }
}

__attribute__((hot)) void dominoTimeGetRuntimeMs(domino_runtime_ms_t* runtime_ms_ptr) {
    if (runtime_ms_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "读取业务毫秒失败: 输出指针为空");
    }
    uint64_t runtime_ns;
    dominoTimeModuleGetDateTimeNow(&runtime_ns);
    // 先校验完整毫秒范围，包含 UINT32_MAX 毫秒内的亚毫秒部分；不截断、回绕、饱和或归零。
    if (runtime_ns >= (UINT64_C(1) + UINT32_MAX) * UINT64_C(1000000)) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "累计时间超出 32 位业务毫秒范围: runtime_ns=%" PRIu64, runtime_ns);
    }
    *runtime_ms_ptr = (domino_runtime_ms_t)(runtime_ns / UINT64_C(1000000));
}
