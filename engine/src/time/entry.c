#include "entry.h"

#include <inttypes.h>
#include <stdatomic.h>

#include "../logger/entry.h"
#include "domino_shared_common.h"

/** @brief 底层以 64 位纳秒保留采样精度；业务时刻和时长始终使用 domino_runtime_ms_t。 */
typedef struct DominoTimeSnapshot {
    uint64_t session_start_monotonic_ns;
    uint64_t runtime_datetime_base_ns;
    bool frozen;
} DominoTimeSnapshot;

/** @brief 生命周期控制方唯一写入；读者仅接受 sequence 相同且为偶数的原子字段快照。 */
typedef struct DominoTimeCache {
    _Atomic uint64_t sequence;
    _Atomic uint64_t session_start_monotonic_ns;
    _Atomic uint64_t runtime_datetime_base_ns;
    _Atomic bool frozen;
} DominoTimeCache;

static DominoTimeCache g_domino_time_cache = {0};

/** @brief 发布快照并关闭写入窗口；调用方已将 sequence 置为奇数，且持有唯一写入权。 */
static void publishTimeCache(const DominoTimeSnapshot* next_ptr) {
    atomic_store(&g_domino_time_cache.session_start_monotonic_ns, next_ptr->session_start_monotonic_ns);
    atomic_store(&g_domino_time_cache.runtime_datetime_base_ns, next_ptr->runtime_datetime_base_ns);
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

DOMINO_CODE dominoTimeModuleInitAfter(void) {
    DominoTimeSnapshot snapshot = readTimeCache();
    DOMINO_CODE result = dominoMonotonicTimeNsChecked(&snapshot.session_start_monotonic_ns);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "初始化时间采样失败: code=%d", result);
        return result;
    }
    snapshot.frozen = true;
    writeTimeCache(&snapshot);
    return CODE_OK;
}

/** @brief 只计算时间，不记录日志；冻结发布窗口内调用时不得访问其他模块。 */
static DOMINO_CODE calculateRuntimeNs(const DominoTimeSnapshot* snapshot_ptr, uint64_t now_monotonic_ns, uint64_t* runtime_ns_ptr) {
    if (now_monotonic_ns < snapshot_ptr->session_start_monotonic_ns) {
        return ERR_CLOCK_READ;
    }
    const uint64_t elapsed_real_ns = now_monotonic_ns - snapshot_ptr->session_start_monotonic_ns;
    if (elapsed_real_ns > UINT64_MAX - snapshot_ptr->runtime_datetime_base_ns) {
        return ERR_OUT_OF_RANGE;
    }
    *runtime_ns_ptr = snapshot_ptr->runtime_datetime_base_ns + elapsed_real_ns;
    return CODE_OK;
}

DOMINO_CODE dominoTimeModuleFreeze(void) {
    DominoTimeSnapshot snapshot = readTimeCache();
    if (snapshot.frozen) {
        return CODE_OK;
    }
    /* 奇数窗口只做时钟采样与缓存写入；失败也先关闭窗口，再记录日志。 */
    atomic_fetch_or(&g_domino_time_cache.sequence, 1);
    uint64_t now_monotonic_ns = 0;
    DOMINO_CODE code = dominoMonotonicTimeNsChecked(&now_monotonic_ns);
    if (code == CODE_OK) {
        code = calculateRuntimeNs(&snapshot, now_monotonic_ns, &snapshot.runtime_datetime_base_ns);
    }
    if (code == CODE_OK) {
        snapshot.session_start_monotonic_ns = now_monotonic_ns;
        snapshot.frozen = true;
    }
    publishTimeCache(&snapshot);
    if (code != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR,
                          "冻结时间失败，采样不可用或累计纳秒越界: sampled_ns=%" PRIu64 ", start_ns=%" PRIu64 ", code=%d", now_monotonic_ns,
                          snapshot.session_start_monotonic_ns, code);
    }
    return code;
}

DOMINO_CODE dominoTimeModuleThaw(void) {
    DominoTimeSnapshot snapshot = readTimeCache();
    if (!snapshot.frozen) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "解冻时间失败: 时钟未冻结");
        return ERR_GAME_STATE_INVALID;
    }

    DOMINO_CODE code = dominoMonotonicTimeNsChecked(&snapshot.session_start_monotonic_ns);
    if (code != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "解冻时间采样失败: code=%d", code);
        return code;
    }
    snapshot.frozen = false;
    writeTimeCache(&snapshot);

    return CODE_OK;
}

__attribute__((hot)) bool dominoTimeModuleIsFrozen(void) {
    return readTimeCache().frozen;
}

__attribute__((hot)) DOMINO_CODE dominoTimeModuleGetDateTimeNow(uint64_t* runtime_ns_ptr) {
    if (runtime_ns_ptr == nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "读取累计纳秒失败: 输出指针为空");
        return ERR_NULL_POINTER;
    }
    while (true) {
        uint64_t before = atomic_load(&g_domino_time_cache.sequence);
        if ((before & 1U) != 0U) {
            continue;
        }

        DominoTimeSnapshot snapshot = loadTimeCacheFields();
        uint64_t monotonic_now_ns = 0;
        DOMINO_CODE code = CODE_OK;
        /* 时钟采样纳入版本校验，避免并发冻结后仍按旧状态计算更晚的时间。 */
        if (!snapshot.frozen) {
            code = dominoMonotonicTimeNsChecked(&monotonic_now_ns);
        }

        uint64_t after = atomic_load(&g_domino_time_cache.sequence);
        if (before != after) {
            continue;
        }
        if (code == CODE_OK && snapshot.frozen) {
            *runtime_ns_ptr = snapshot.runtime_datetime_base_ns;
            return CODE_OK;
        }
        if (code == CODE_OK) {
            code = calculateRuntimeNs(&snapshot, monotonic_now_ns, runtime_ns_ptr);
        }
        if (code != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR,
                              "读取累计纳秒失败，采样不可用或累计值越界: sampled_ns=%" PRIu64 ", start_ns=%" PRIu64 ", code=%d", monotonic_now_ns,
                              snapshot.session_start_monotonic_ns, code);
        }
        return code;
    }
}

__attribute__((hot)) DOMINO_CODE dominoTimeGetRuntimeMs(domino_runtime_ms_t* runtime_ms_ptr) {
    if (runtime_ms_ptr == nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "读取业务毫秒失败: 输出指针为空");
        return ERR_NULL_POINTER;
    }
    uint64_t runtime_ns;
    DOMINO_CODE code = dominoTimeModuleGetDateTimeNow(&runtime_ns);
    if (code != CODE_OK) {
        return code;
    }
    // 先校验完整毫秒范围，包含 UINT32_MAX 毫秒内的亚毫秒部分；失败时保持输出原值。
    if (runtime_ns >= (UINT64_C(1) + UINT32_MAX) * UINT64_C(1000000)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "累计时间超出 32 位业务毫秒范围: runtime_ns=%" PRIu64, runtime_ns);
        return ERR_OUT_OF_RANGE;
    }
    *runtime_ms_ptr = (domino_runtime_ms_t)(runtime_ns / UINT64_C(1000000));
    return CODE_OK;
}
