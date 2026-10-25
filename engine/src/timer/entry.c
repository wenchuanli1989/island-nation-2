#include "entry.h"

#include <string.h>

#include "../queue/thread_message.h"
#include "../time/entry.h"
#include "internal.h"

DominoTimerModule g_domino_timer;

void dominoTimerModuleInit(void) {
    dominoThreadQueueInit(&g_domino_timer.command_queue, sizeof(DominoTimer), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    DOMINO_REQUIRE_THREAD_SUCCESS(mtx_init(&g_domino_timer.request_mutex, mtx_plain), ERR_MUTEX_INIT, "mtx_init");
    DOMINO_REQUIRE_THREAD_SUCCESS(cnd_init(&g_domino_timer.wake_condition), ERR_CONDITION_INIT, "cnd_init");
}

void dominoTimerModuleStart(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_domino_timer.thread, dominoTimerThread, nullptr), ERR_THREAD_CREATE, "thrd_create");
}

void dominoTimerModuleStop(void) {
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_domino_timer.thread, nullptr), ERR_THREAD_JOIN, "thrd_join");
}

void dominoTimerModuleDestroy(void) {
    dominoThreadQueueDestroy(&g_domino_timer.command_queue);
    dominoTimerHeapDestroy(&g_domino_timer.heap);
    cnd_destroy(&g_domino_timer.wake_condition);
    mtx_destroy(&g_domino_timer.request_mutex);
    memset(&g_domino_timer, 0, sizeof(g_domino_timer));
}

void dominoTimerNotifyTimeChanged(void) {
    notifyTimerThread();
}

/**
 * @brief 只校验共同登记条件，不修改请求或采样业务时刻。
 * @pre 登记入口已确认 timer_ptr 非空。
 */
static DOMINO_CODE validateTimerRequest(const DominoTimer* timer_ptr) {
    if (timer_ptr->cancelled_flag) {
        return ERR_INVALID_PARAM;
    }
    if (timer_ptr->event_queue_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (timer_ptr->event_queue_ptr->item_size != sizeof(DominoThreadMessage)) {
        return ERR_INVALID_PARAM;
    }
    return CODE_OK;
}

/** @brief 按值提交已校验的请求；调用方已填写绝对截止点。 */
static void submitTimerRequest(DominoTimer* timer_ptr) {
    DOMINO_CODE code = dominoThreadQueueProduce(&g_domino_timer.command_queue, timer_ptr);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "提交计时器登记命令失败");
    }
    notifyTimerThread();
}

__attribute__((hot)) DOMINO_CODE dominoTimerSchedule(DominoTimer* timer_ptr) {
    if (timer_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    timer_ptr->timer_id = 0U;
    DOMINO_CODE code = validateTimerRequest(timer_ptr);
    if (code != CODE_OK) {
        return code;
    }
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    if (timer_ptr->delay_ms > UINT32_MAX - now_ms) {
        return ERR_OUT_OF_RANGE;
    }
    timer_ptr->due_ms = now_ms + timer_ptr->delay_ms;
    submitTimerRequest(timer_ptr);
    return CODE_OK;
}

__attribute__((hot)) DOMINO_CODE dominoTimerScheduleAt(DominoTimer* timer_ptr, domino_runtime_ms_t due_ms) {
    if (timer_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    timer_ptr->timer_id = 0U;
    DOMINO_CODE code = validateTimerRequest(timer_ptr);
    if (code != CODE_OK) {
        return code;
    }
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    timer_ptr->due_ms = due_ms;
    timer_ptr->delay_ms = due_ms > now_ms ? due_ms - now_ms : 0U;
    submitTimerRequest(timer_ptr);
    return CODE_OK;
}
