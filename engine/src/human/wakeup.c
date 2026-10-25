#include "wakeup.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_common.h"
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
static void cancelCurrentWakeup(DominoHuman* human_ptr) {
    const domino_timer_id_t old_timer_id = human_ptr->wakeup_timer_id;
    human_ptr->wakeup_timer_id = 0U;
    human_ptr->wakeup_version++;

    if (old_timer_id != 0U) {
        const khint_t map_index = humanWakeupIDMap_get(g_wakeup_id_map, old_timer_id);
        if (kh_exist(g_wakeup_id_map, map_index)) {
            humanWakeupIDMap_del(g_wakeup_id_map, map_index);
        }

        const DOMINO_CODE code = dominoTimerCancel(old_timer_id, g_wakeup_event_queue_ptr);
        if (code != CODE_OK && code != ERR_NOT_FOUND) {
            DOMINO_EXIT_WITH_ERROR(code, "cannot cancel human=%" PRIu32 " wakeup timer=%" PRIu64, human_ptr->human_id, old_timer_id);
        }
    }
}

void dominoHumanWakeupInit(DominoThreadQueue* event_queue_ptr) {
    g_wakeup_id_map = humanWakeupIDMap_init();
    if (!g_wakeup_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human wakeup ID map");
    }
    g_wakeup_event_queue_ptr = event_queue_ptr;
}

void dominoHumanWakeupDestroy(void) {
    humanWakeupIDMap_destroy(g_wakeup_id_map);
    g_wakeup_id_map = nullptr;
    g_wakeup_event_queue_ptr = nullptr;
    for (size_t index = 0U; index < kv_size(domino_all_human_list); index++) {
        DominoHuman* human_ptr = &kv_A(domino_all_human_list, index);
        human_ptr->wakeup_timer_id = 0U;
        human_ptr->wakeup_version = 0U;
    }
}

void dominoHumanWakeupSchedule(DominoHuman* human_ptr, domino_runtime_ms_t due_ms) {
    if (human_ptr->wakeup_timer_id != 0U) {
        const khint_t map_index = humanWakeupIDMap_get(g_wakeup_id_map, human_ptr->wakeup_timer_id);
        if (!kh_exist(g_wakeup_id_map, map_index)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "human=%" PRIu32 " references missing wakeup timer=%" PRIu64, human_ptr->human_id,
                                   human_ptr->wakeup_timer_id);
        }
        const HumanWakeupAssociation association = kh_val(g_wakeup_id_map, map_index);
        if (association.human_id != human_ptr->human_id || association.wakeup_version != human_ptr->wakeup_version) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "human=%" PRIu32 " wakeup timer=%" PRIu64 " association identity/version mismatch",
                                   human_ptr->human_id, human_ptr->wakeup_timer_id);
        }
        if (association.due_ms == due_ms) {
            return;
        }
    }
    if (human_ptr->wakeup_version == UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "human=%" PRIu32 " wakeup version cannot advance", human_ptr->human_id);
    }
    cancelCurrentWakeup(human_ptr);

    DominoTimer timer = {
        .event_queue_ptr = g_wakeup_event_queue_ptr,
        .event_type = HUMAN_WAKEUP_EVENT_TYPE,
    };
    const DOMINO_CODE code = dominoTimerScheduleAt(&timer, due_ms);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "cannot schedule human=%" PRIu32 " wakeup due_ms=%" PRIu32, human_ptr->human_id, due_ms);
    }
    const khint_t capacity = kh_capacity(g_wakeup_id_map);
    if (kh_size(g_wakeup_id_map) >= kh_max_count(capacity)) {
        if (capacity >= ((khint_t)1U << 31U)) {
            DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "human wakeup ID map capacity exhausted: human=%" PRIu32 " capacity=%u",
                                   human_ptr->human_id, (unsigned)capacity);
        }
        const khint_t new_capacity = capacity == 0U ? 4U : capacity * 2U;
        if ((size_t)new_capacity > SIZE_MAX / sizeof(HumanWakeupIDMap_m_bucket_t)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "human wakeup ID map allocation size overflows capacity=%u", (unsigned)new_capacity);
        }
    }
    int absent_flag;
    const khint_t map_index = humanWakeupIDMap_put(g_wakeup_id_map, timer.timer_id, &absent_flag);
    if (absent_flag < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot insert human=%" PRIu32 " wakeup timer=%" PRIu64, human_ptr->human_id, timer.timer_id);
    }
    /* timer 命令队列在本生命周期内单调取号且禁止回绕。 */
    if (absent_flag != 1) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "human wakeup timer ID is already registered: timer_id=%" PRIu64, timer.timer_id);
    }
    kh_val(g_wakeup_id_map, map_index) = (HumanWakeupAssociation){
        .human_id = human_ptr->human_id,
        .wakeup_version = human_ptr->wakeup_version,
        .due_ms = due_ms,
    };
    human_ptr->wakeup_timer_id = timer.timer_id;
}

void dominoHumanWakeupCancel(DominoHuman* human_ptr) {
    if (human_ptr->wakeup_timer_id == 0U) {
        return;
    }
    if (human_ptr->wakeup_version == UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_OUT_OF_RANGE, "human=%" PRIu32 " wakeup version cannot advance on cancellation", human_ptr->human_id);
    }
    cancelCurrentWakeup(human_ptr);
}

DOMINO_CODE dominoHumanWakeupConsume(const DominoTimer* timer_ptr, DominoHuman** human_out_ptr) {
    if (!timer_ptr || !human_out_ptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "human wakeup consume requires a timer and human output");
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
