#include "entry.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include <threads.h>

#include "../entry.h"
#include "domino_shared_common.h"

/** @brief 时间状态的普通副本；内部用 64 位纳秒保留精度，业务只用 domino_runtime_ms_t。 */
typedef struct DominoTimeSnapshot {
    uint64_t session_start_monotonic_ns;
    uint64_t runtime_base_ns;
    uint64_t freeze_epoch;
    bool frozen;
} DominoTimeSnapshot;

/** @brief 以 sequence 保护的时间状态（宪法 7.3）：只有生命周期控制方写入；读者只接受前后 sequence 相同且为偶数的读取。 */
typedef struct DominoTimeState {
    _Atomic uint64_t sequence;
    _Atomic uint64_t session_start_monotonic_ns;
    _Atomic uint64_t runtime_base_ns;
    _Atomic uint64_t freeze_epoch;
    _Atomic bool frozen;
} DominoTimeState;

static DominoTimeState g_time_state = {.freeze_epoch = 1U, .frozen = true};

/** @brief 冻结切换、等待登记和通知共用的锁；waiter_count 只供显示。 */
typedef struct DominoFreezeWait {
    cnd_t condition;
    mtx_t mutex;
    uint32_t waiter_count;
} DominoFreezeWait;

static DominoFreezeWait g_freeze_wait;

/** @brief 写入各字段后把 sequence 加一回到偶数；调用方已把 sequence 置为奇数。 */
static void publishTimeState(const DominoTimeSnapshot* next_ptr) {
    atomic_store(&g_time_state.session_start_monotonic_ns, next_ptr->session_start_monotonic_ns);
    atomic_store(&g_time_state.runtime_base_ns, next_ptr->runtime_base_ns);
    atomic_store(&g_time_state.freeze_epoch, next_ptr->freeze_epoch);
    atomic_store(&g_time_state.frozen, next_ptr->frozen);
    atomic_fetch_add(&g_time_state.sequence, 1);
}

static void writeTimeState(const DominoTimeSnapshot* next_ptr) {
    atomic_fetch_or(&g_time_state.sequence, 1);
    publishTimeState(next_ptr);
}

/** @brief 逐个读取原子字段；一致性由调用方比较 sequence 保证。 */
static inline DominoTimeSnapshot loadTimeStateFields(void) {
    return (DominoTimeSnapshot){
        .session_start_monotonic_ns = atomic_load(&g_time_state.session_start_monotonic_ns),
        .runtime_base_ns = atomic_load(&g_time_state.runtime_base_ns),
        .freeze_epoch = atomic_load(&g_time_state.freeze_epoch),
        .frozen = atomic_load(&g_time_state.frozen),
    };
}

/** @brief 重试直到读到一份一致的时间状态。 */
static inline DominoTimeSnapshot readTimeState(void) {
    while (true) {
        uint64_t before = atomic_load(&g_time_state.sequence);
        if ((before & 1U) != 0U) {
            continue;
        }

        DominoTimeSnapshot snapshot = loadTimeStateFields();
        uint64_t after = atomic_load(&g_time_state.sequence);
        if (before == after) {
            return snapshot;
        }
    }
}

uint64_t dominoTimeGetRuntimeBaseNs(void) {
    return readTimeState().runtime_base_ns;
}

void dominoTimeSetRuntimeBaseNs(uint64_t base_ns) {
    DominoTimeSnapshot snapshot = readTimeState();
    snapshot.runtime_base_ns = base_ns;
    writeTimeState(&snapshot);
}

void dominoTimeModuleInit(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_init(&g_freeze_wait.mutex, mtx_plain), ERR_MUTEX_INIT, "time:mtx_init");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&g_freeze_wait.condition), ERR_CONDITION_INIT, "time:cnd_init");
}

void dominoTimeModuleDestroy(void) {
    cnd_destroy(&g_freeze_wait.condition);
    mtx_destroy(&g_freeze_wait.mutex);
    memset(&g_freeze_wait, 0, sizeof(g_freeze_wait));
    atomic_store_explicit(&g_time_state.session_start_monotonic_ns, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_time_state.runtime_base_ns, 0U, memory_order_relaxed);
    atomic_store_explicit(&g_time_state.freeze_epoch, 1U, memory_order_relaxed);
    atomic_store_explicit(&g_time_state.frozen, true, memory_order_relaxed);
    atomic_store_explicit(&g_time_state.sequence, 0U, memory_order_relaxed);
}

/** @brief 基准 + 本次解冻以来的单调时间；时钟倒退或溢出致命退出。 */
static uint64_t calculateRuntimeNs(const DominoTimeSnapshot* snapshot_ptr, uint64_t now_monotonic_ns) {
    if (now_monotonic_ns < snapshot_ptr->session_start_monotonic_ns) {
        DOMINO_EXIT_WITH_ERROR(ERR_CLOCK_READ, "单调时钟倒退: sampled_ns=%" PRIu64 ", start_ns=%" PRIu64, now_monotonic_ns,
                               snapshot_ptr->session_start_monotonic_ns);
    }
    const uint64_t elapsed_real_ns = now_monotonic_ns - snapshot_ptr->session_start_monotonic_ns;
    if (elapsed_real_ns > UINT64_MAX - snapshot_ptr->runtime_base_ns) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "累计纳秒溢出: base_ns=%" PRIu64 ", elapsed_ns=%" PRIu64, snapshot_ptr->runtime_base_ns,
                               elapsed_real_ns);
    }
    return snapshot_ptr->runtime_base_ns + elapsed_real_ns;
}

void dominoTimeModuleFreeze(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_freeze_wait.mutex), ERR_MUTEX_LOCK, "time freeze:mtx_lock");
    DominoTimeSnapshot snapshot = readTimeState();
    if (!snapshot.frozen) {
        if (snapshot.freeze_epoch == UINT64_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "冻结轮次耗尽");
        }
        // 先把 sequence 置为奇数再采样，读者就不会接受晚于冻结基准的时刻。
        atomic_fetch_or(&g_time_state.sequence, 1);
        uint64_t now_monotonic_ns = dominoGetMonotonicTimeNs();
        snapshot.runtime_base_ns = calculateRuntimeNs(&snapshot, now_monotonic_ns);
        snapshot.session_start_monotonic_ns = now_monotonic_ns;
        snapshot.freeze_epoch++;
        snapshot.frozen = true;
        g_freeze_wait.waiter_count = 0U;
        publishTimeState(&snapshot);
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_freeze_wait.mutex), ERR_MUTEX_UNLOCK, "time freeze:mtx_unlock");
}

void dominoTimeModuleThaw(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_freeze_wait.mutex), ERR_MUTEX_LOCK, "time thaw:mtx_lock");
    DominoTimeSnapshot snapshot = readTimeState();
    if (!snapshot.frozen) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "解冻时间失败: 时钟未冻结");
    }

    snapshot.session_start_monotonic_ns = dominoGetMonotonicTimeNs();
    snapshot.frozen = false;
    writeTimeState(&snapshot);
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&g_freeze_wait.condition), ERR_CONDITION_BROADCAST, "time thaw:cnd_broadcast");
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_freeze_wait.mutex), ERR_MUTEX_UNLOCK, "time thaw:mtx_unlock");
}

__attribute__((hot)) bool dominoTimeModuleIsFrozen(void) {
    return readTimeState().frozen;
}

__attribute__((hot)) uint64_t dominoTimeModuleGetFreezeEpoch(void) {
    const DominoTimeSnapshot snapshot = readTimeState();
    return snapshot.frozen ? snapshot.freeze_epoch : 0U;
}

void dominoTimeModuleWait(uint64_t observed_epoch) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_freeze_wait.mutex), ERR_MUTEX_LOCK, "time wait:mtx_lock");
    for (;;) {
        const DominoTimeSnapshot snapshot = readTimeState();
        if (dominoEngineIsStopping() || !snapshot.frozen || snapshot.freeze_epoch != observed_epoch) {
            break;
        }
        g_freeze_wait.waiter_count++;
        const int result_code = cnd_wait(&g_freeze_wait.condition, &g_freeze_wait.mutex);
        // 被唤醒到重新持锁之间可能已解冻又冻结；只有仍是本轮时才扣减计数，不扣新一轮的人数。
        if (atomic_load(&g_time_state.freeze_epoch) == observed_epoch) {
            g_freeze_wait.waiter_count--;
        }
        DOMINO_REQUIRE_THREAD_SUCCESS(result_code, ERR_CONDITION_WAIT, "time wait:cnd_wait");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_freeze_wait.mutex), ERR_MUTEX_UNLOCK, "time wait:mtx_unlock");
}

void dominoTimeModuleWakeAll(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_freeze_wait.mutex), ERR_MUTEX_LOCK, "time wake:mtx_lock");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_broadcast(&g_freeze_wait.condition), ERR_CONDITION_BROADCAST, "time wake:cnd_broadcast");
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_freeze_wait.mutex), ERR_MUTEX_UNLOCK, "time wake:mtx_unlock");
}

uint32_t dominoTimeModuleGetFreezeWaiterCount(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_lock(&g_freeze_wait.mutex), ERR_MUTEX_LOCK, "time count:mtx_lock");
    const uint32_t waiter_count = g_freeze_wait.waiter_count;
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_unlock(&g_freeze_wait.mutex), ERR_MUTEX_UNLOCK, "time count:mtx_unlock");
    return waiter_count;
}

__attribute__((hot)) void dominoTimeGetRuntimeNs(uint64_t* runtime_ns_ptr) {
    if (runtime_ns_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "读取累计纳秒失败: 输出指针为空");
    }
    while (true) {
        uint64_t before = atomic_load(&g_time_state.sequence);
        if ((before & 1U) != 0U) {
            continue;
        }

        DominoTimeSnapshot snapshot = loadTimeStateFields();
        uint64_t monotonic_now_ns = 0;
        // 采样放在两次 sequence 读取之间：并发冻结会让本次读取作废，避免按旧状态算出晚于冻结时刻的时间。
        if (!snapshot.frozen) {
            monotonic_now_ns = dominoGetMonotonicTimeNs();
        }

        uint64_t after = atomic_load(&g_time_state.sequence);
        if (before != after) {
            continue;
        }
        if (snapshot.frozen) {
            *runtime_ns_ptr = snapshot.runtime_base_ns;
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
    dominoTimeGetRuntimeNs(&runtime_ns);
    // 校验整个毫秒范围（含 UINT32_MAX 毫秒内的亚毫秒部分），不截断、回绕或饱和。
    if (runtime_ns >= (UINT64_C(1) + UINT32_MAX) * UINT64_C(1000000)) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "累计时间超出 32 位业务毫秒范围: runtime_ns=%" PRIu64, runtime_ns);
    }
    *runtime_ms_ptr = (domino_runtime_ms_t)(runtime_ns / UINT64_C(1000000));
}
