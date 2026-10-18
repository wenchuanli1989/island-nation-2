#include "wakeup.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "../logger/entry.h"
#include "entry.h"
#include "klib/khashl.h"

typedef struct HumanWakeupAssociation {
    domino_human_id_t human_id;
    uint32_t wakeup_version;
    domino_runtime_ms_t due_ms;
} HumanWakeupAssociation;

KHASHL_MAP_INIT(KH_LOCAL, HumanWakeupIDMap, humanWakeupIDMap, domino_timer_id_t, HumanWakeupAssociation, kh_hash_uint64, kh_eq_generic)

static HumanWakeupIDMap* g_wakeup_id_map;
static DominoThreadQueue* g_wakeup_event_queue_ptr;

enum {
    HUMAN_WAKEUP_EVENT_TYPE = 1U,
};

/**
 * @brief 清除人物的唤醒登记，再取消旧计时器；已出队的旧通知由消费端忽略。
 * @pre 调用方已检查 wakeup_version 可以递增。
 */
static DOMINO_CODE cancelCurrentWakeup(DominoHuman* human_ptr) {
    const domino_timer_id_t old_timer_id = human_ptr->wakeup_timer_id;
    human_ptr->wakeup_timer_id = 0U;
    human_ptr->wakeup_version++;

    if (old_timer_id != 0U) {
        const khint_t map_index = humanWakeupIDMap_get(g_wakeup_id_map, old_timer_id);
        if (kh_exist(g_wakeup_id_map, map_index)) {
            humanWakeupIDMap_del(g_wakeup_id_map, map_index);
        }

        const DOMINO_CODE code = dominoTimerCancel(old_timer_id, g_wakeup_event_queue_ptr);
        if (code == ERR_NOT_FOUND) {
            return CODE_OK;
        } else {
            return code;
        }
    } else {
        return CODE_OK;
    }
}

DOMINO_CODE dominoHumanWakeupInit(DominoThreadQueue* event_queue_ptr) {
    if (g_wakeup_id_map) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human wakeup associations are already initialized");
        return ERR_ALREADY_INITIALIZED;
    }
    if (!event_queue_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human wakeup event queue is null");
        return ERR_NULL_POINTER;
    }
    if (!event_queue_ptr->initialized || !dominoHumanIdMap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human wakeup event queue or human ID map is not initialized");
        return ERR_NOT_INITIALIZED;
    }
    if (event_queue_ptr->item_size != sizeof(DominoThreadMessage)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human wakeup queue item size=%zu expected=%zu",
                          event_queue_ptr->item_size, sizeof(DominoThreadMessage));
        return ERR_INVALID_PARAM;
    }
    g_wakeup_id_map = humanWakeupIDMap_init();
    if (!g_wakeup_id_map) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot allocate human wakeup ID map");
        return ERR_MEMORY_ALLOC;
    }
    g_wakeup_event_queue_ptr = event_queue_ptr;
    return CODE_OK;
}

void dominoHumanWakeupExit(void) {
    if (!g_wakeup_id_map) {
        return;
    }
    humanWakeupIDMap_destroy(g_wakeup_id_map);
    g_wakeup_id_map = nullptr;
    g_wakeup_event_queue_ptr = nullptr;
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        human_ptr->wakeup_timer_id = 0U;
        human_ptr->wakeup_version = 0U;
    }
}

DOMINO_CODE dominoHumanWakeupSchedule(DominoHuman* human_ptr, domino_runtime_ms_t due_ms) {
    if (!g_wakeup_id_map) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot schedule human wakeup before initialization");
        return ERR_NOT_INITIALIZED;
    }
    if (human_ptr->wakeup_timer_id != 0U) {
        const khint_t map_index = humanWakeupIDMap_get(g_wakeup_id_map, human_ptr->wakeup_timer_id);
        if (!kh_exist(g_wakeup_id_map, map_index)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human=%" PRIu32 " references missing wakeup timer=%" PRIu64,
                              human_ptr->human_id, human_ptr->wakeup_timer_id);
            return ERR_INVALID_DATA;
        }
        const HumanWakeupAssociation association = kh_val(g_wakeup_id_map, map_index);
        if (association.human_id != human_ptr->human_id || association.wakeup_version != human_ptr->wakeup_version) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "human=%" PRIu32 " wakeup timer=%" PRIu64 " association identity/version mismatch", human_ptr->human_id,
                              human_ptr->wakeup_timer_id);
            return ERR_INVALID_DATA;
        }
        if (association.due_ms == due_ms) {
            return CODE_OK;
        }
    }
    if (human_ptr->wakeup_version == UINT32_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human=%" PRIu32 " wakeup version cannot advance",
                          human_ptr->human_id);
        return ERR_OUT_OF_RANGE;
    }
    DOMINO_CODE code = cancelCurrentWakeup(human_ptr);
    if (code != CODE_OK) {
        return code;
    }

    DominoTimer timer = {
        .event_queue_ptr = g_wakeup_event_queue_ptr,
        .event_type = HUMAN_WAKEUP_EVENT_TYPE,
    };
    code = dominoTimerScheduleAt(&timer, due_ms);
    if (code != CODE_OK) {
        return code;
    }
    const khint_t capacity = kh_capacity(g_wakeup_id_map);
    if (kh_size(g_wakeup_id_map) >= kh_max_count(capacity)) {
        if (capacity >= ((khint_t)1U << 31U)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human wakeup ID map capacity exhausted capacity=%u",
                              (unsigned)capacity);
            return ERR_CAPACITY_EXCEEDED;
        }
        const khint_t new_capacity = capacity == 0U ? 4U : capacity * 2U;
        if ((size_t)new_capacity > SIZE_MAX / sizeof(HumanWakeupIDMap_m_bucket_t)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human wakeup ID map allocation size overflows capacity=%u",
                              (unsigned)new_capacity);
            return ERR_MEMORY_OVERFLOW;
        }
    }
    int absent_flag;
    const khint_t map_index = humanWakeupIDMap_put(g_wakeup_id_map, timer.timer_id, &absent_flag);
    if (absent_flag < 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot insert human=%" PRIu32 " wakeup timer=%" PRIu64,
                          human_ptr->human_id, timer.timer_id);
        return ERR_MEMORY_ALLOC;
    }
    /* timer 命令队列在本生命周期内单调取号且禁止回绕。 */
    assert(absent_flag == 1);
    kh_val(g_wakeup_id_map, map_index) = (HumanWakeupAssociation){
        .human_id = human_ptr->human_id,
        .wakeup_version = human_ptr->wakeup_version,
        .due_ms = due_ms,
    };
    human_ptr->wakeup_timer_id = timer.timer_id;
    return CODE_OK;
}

DOMINO_CODE dominoHumanWakeupCancel(DominoHuman* human_ptr) {
    if (!g_wakeup_id_map) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot cancel human wakeup before initialization");
        return ERR_NOT_INITIALIZED;
    }
    if (human_ptr->wakeup_timer_id == 0U) {
        return CODE_OK;
    }
    if (human_ptr->wakeup_version == UINT32_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human=%" PRIu32 " wakeup version cannot advance on cancellation",
                          human_ptr->human_id);
        return ERR_OUT_OF_RANGE;
    }
    return cancelCurrentWakeup(human_ptr);
}

DOMINO_CODE dominoHumanWakeupConsume(const DominoTimer* timer_ptr, DominoHuman** human_out_ptr) {
    if (!timer_ptr || !human_out_ptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "human wakeup consume requires a timer and human output");
        return ERR_NULL_POINTER;
    }
    if (!g_wakeup_id_map) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR, "cannot consume human wakeup before initialization");
        return ERR_NOT_INITIALIZED;
    }
    if (timer_ptr->event_queue_ptr != g_wakeup_event_queue_ptr || timer_ptr->event_type != HUMAN_WAKEUP_EVENT_TYPE) {
        return ERR_NOT_FOUND;
    }
    const khint_t map_index = humanWakeupIDMap_get(g_wakeup_id_map, timer_ptr->timer_id);
    if (!kh_exist(g_wakeup_id_map, map_index)) {
        return ERR_NOT_FOUND;
    }
    const HumanWakeupAssociation association = kh_val(g_wakeup_id_map, map_index);
    humanWakeupIDMap_del(g_wakeup_id_map, map_index);
    DominoHuman* human_ptr = dominoGetHumanByID(association.human_id);
    if (!human_ptr || human_ptr->wakeup_timer_id != timer_ptr->timer_id || human_ptr->wakeup_version != association.wakeup_version) {
        return ERR_NOT_FOUND;
    }
    human_ptr->wakeup_timer_id = 0U;
    if (timer_ptr->cancelled_flag) {
        return ERR_NOT_FOUND;
    }
    *human_out_ptr = human_ptr;
    return CODE_OK;
}
