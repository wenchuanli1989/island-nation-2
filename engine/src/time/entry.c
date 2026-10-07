#include "entry.h"

#include <stdatomic.h>

#include "../../include/domino_engine.h"
#include "../entry.h"
#include "domino_shared_common.h"

typedef struct DominoTimeSnapshot {
    uint64_t session_start_monotonic_ns;
    uint64_t runtime_datetime_base_ns;
    bool frozen;
} DominoTimeSnapshot;

/** @brief 客户端线程串行写入；读者仅接受 sequence 相同且为偶数的原子字段快照。 */
typedef struct DominoTimeCache {
    _Atomic uint64_t sequence;
    _Atomic uint64_t session_start_monotonic_ns;
    _Atomic uint64_t runtime_datetime_base_ns;
    _Atomic bool frozen;
} DominoTimeCache;

static DominoTimeCache g_domino_time_cache = {0};

static void writeTimeCache(const DominoTimeSnapshot* next_ptr) {
    atomic_fetch_or(&g_domino_time_cache.sequence, 1);
    atomic_store(&g_domino_time_cache.session_start_monotonic_ns, next_ptr->session_start_monotonic_ns);
    atomic_store(&g_domino_time_cache.runtime_datetime_base_ns, next_ptr->runtime_datetime_base_ns);
    atomic_store(&g_domino_time_cache.frozen, next_ptr->frozen);
    atomic_fetch_add(&g_domino_time_cache.sequence, 1);
}

/** @brief 仅加载原子字段；调用方负责通过 sequence 验证快照一致性。 */
static inline DominoTimeSnapshot loadTimeCacheFields(void) {
    return (DominoTimeSnapshot){
        .session_start_monotonic_ns = atomic_load(&g_domino_time_cache.session_start_monotonic_ns),
        .runtime_datetime_base_ns = atomic_load(&g_domino_time_cache.runtime_datetime_base_ns),
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

void dominoTimeModuleInitBefore(void) {
    const DominoTimeSnapshot snapshot = {
        .session_start_monotonic_ns = 0,
        .runtime_datetime_base_ns = 0,
        .frozen = true,
    };
    writeTimeCache(&snapshot);
}

void dominoTimeModuleInitAfter(void) {
    DominoTimeSnapshot snapshot = readTimeCache();
    snapshot.session_start_monotonic_ns = dominoMonotonicTimeNs();
    snapshot.frozen = false;
    writeTimeCache(&snapshot);
}

static void update_runtime_datetime_base(DominoTimeSnapshot* snapshot_ptr) {
    uint64_t now_monotonic_ns = dominoMonotonicTimeNs();
    uint64_t elapsed_real_ns = now_monotonic_ns - snapshot_ptr->session_start_monotonic_ns;
    snapshot_ptr->runtime_datetime_base_ns += elapsed_real_ns;
    snapshot_ptr->session_start_monotonic_ns = now_monotonic_ns;
}

void dominoTimeModuleExit(void) {
    DominoTimeSnapshot snapshot = readTimeCache();
    if (!snapshot.frozen) {
        update_runtime_datetime_base(&snapshot);
    }
    snapshot.frozen = true;
    writeTimeCache(&snapshot);
}

DOMINO_CODE dominoEngineFreezeTime(void) {
    if (!dominoEngineIsClientThread()) {
        return ERR_GAME_STATE_INVALID;
    }

    DominoTimeSnapshot snapshot = readTimeCache();
    if (snapshot.frozen) {
        return ERR_GAME_STATE_INVALID;
    }

    /* 先阻止读者采样，避免冻结值落后于并发读者已读到的时间。 */
    atomic_fetch_or(&g_domino_time_cache.sequence, 1);
    update_runtime_datetime_base(&snapshot);
    snapshot.frozen = true;
    writeTimeCache(&snapshot);

    return CODE_OK;
}

DOMINO_CODE dominoEngineThawTime(void) {
    if (!dominoEngineIsClientThread()) {
        return ERR_GAME_STATE_INVALID;
    }

    DominoTimeSnapshot snapshot = readTimeCache();
    if (!snapshot.frozen) {
        return ERR_GAME_STATE_INVALID;
    }

    snapshot.session_start_monotonic_ns = dominoMonotonicTimeNs();
    snapshot.frozen = false;
    writeTimeCache(&snapshot);

    return CODE_OK;
}

__attribute__((hot)) bool dominoTimeModuleIsFrozen(void) {
    return readTimeCache().frozen;
}

__attribute__((hot)) uint64_t dominoTimeModuleGetDateTimeNow(void) {
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
            return snapshot.runtime_datetime_base_ns;
        }
        return snapshot.runtime_datetime_base_ns + (monotonic_now_ns - snapshot.session_start_monotonic_ns);
    }
}

__attribute__((hot)) uint64_t dominoTimeGetRuntimeMs(void) {
    return dominoTimeModuleGetDateTimeNow() / UINT64_C(1000000);
}
