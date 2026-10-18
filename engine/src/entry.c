#include "entry.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "common/entry.h"
#include "dispatch/entry.h"
#include "domino_engine.h"
#include "domino_shared_error_codes.h"
#include "host/entry.h"
#include "human/entry.h"
#include "human/executor.h"
#include "logger/entry.h"
#include "logger/error.h"
#include "nav/entry.h"
#include "queue/thread_queue.h"
#include "social/entry.h"
#include "storage/entry.h"
#include "time/entry.h"
#include "timer/entry.h"

char g_domino_storage_account_id[DOMINO_STORAGE_ACCOUNT_ID_MAX] = "";
char g_domino_storage_run_mode[DOMINO_STORAGE_RUN_MODE_MAX] = "local";

static char g_domino_storage_path[DOMINO_STORAGE_PATH_MAX] = "";

static thrd_t g_domino_client_thread;
static thrd_t g_domino_main_loop_thread;

static DominoThreadQueue g_domino_to_main_loop_queue;
static DominoThreadQueue g_domino_from_main_loop_queue;

typedef enum DominoEngineLifecycleState {
    DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED = 0,
    DOMINO_ENGINE_LIFECYCLE_INITIALIZING,
    DOMINO_ENGINE_LIFECYCLE_INITIALIZED,
    DOMINO_ENGINE_LIFECYCLE_STARTING,
    DOMINO_ENGINE_LIFECYCLE_RUNNING,
    DOMINO_ENGINE_LIFECYCLE_STOPPING,
    DOMINO_ENGINE_LIFECYCLE_FAILED,
} DominoEngineLifecycleState;

static _Atomic DominoEngineLifecycleState g_domino_engine_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED;
static _Atomic DOMINO_CODE g_domino_lifecycle_error = CODE_OK;
static atomic_bool g_domino_stop_requested;
static atomic_bool g_domino_main_loop_completed;
/* completed 的发布使客户端可读主循环线程结束结果。 */
static DOMINO_CODE g_domino_main_loop_result;

DOMINO_CODE dominoEngineGetLifecycleError(void) {
    return atomic_load(&g_domino_lifecycle_error);
}

/** @brief 等待本次操作完成；操作结果由发布方在完成回执之前写入。 */
static DOMINO_CODE waitLifecycleCompletion(const atomic_bool* completed_ptr) {
    while (!atomic_load(completed_ptr)) {
        const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
        if (failure_code != CODE_OK) {
            return failure_code;
        }
        const struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
        if (thrd_sleep(&delay, nullptr) == -2) {
            return DOMINO_LOG_DIRECT_ERROR(ERR_THREAD_SLEEP, "failed to wait for lifecycle completion");
        }
    }
    return dominoEngineGetLifecycleError();
}

/** @brief 发布主循环自身的结束结果；其他线程停止与保存由客户端协调。 */
static DOMINO_CODE completeMainLoop(DOMINO_CODE result) {
    g_domino_main_loop_result = result;
    atomic_store(&g_domino_main_loop_completed, true);
    return result;
}

typedef struct DominoEngineRuntimeNotifications {
    mtx_t mutex;
    bool ready_flag;
} DominoEngineRuntimeNotifications;

static DominoEngineRuntimeNotifications g_domino_runtime_notifications;
static once_flag g_domino_runtime_notifications_once = ONCE_FLAG_INIT;
static DOMINO_CODE g_domino_runtime_notifications_init_result;

DominoEngineLaunchConfig g_domino_engine_launch_config = {};

bool dominoEngineIsClientThread(void) {
    return thrd_equal(thrd_current(), g_domino_client_thread) != 0;
}

bool dominoEngineIsStopRequested(void) {
    return atomic_load(&g_domino_stop_requested);
}

static void checkNotificationResult(int result, DOMINO_CODE code, const char* operation_str) {
    if (result != thrd_success) {
        (void)DOMINO_LOG_DIRECT_ERROR(code, operation_str);
        abort();
    }
}

/** @brief 通知锁保留到进程结束，串行保护异步失败通知与运行队列的撤下。 */
static void initNotificationSynchronization(void) {
    if (mtx_init(&g_domino_runtime_notifications.mutex, mtx_plain) != thrd_success) {
        g_domino_runtime_notifications_init_result = DOMINO_LOG_DIRECT_ERROR(ERR_MUTEX_INIT, "failed to initialize runtime notification mutex");
    }
}

DOMINO_CODE dominoEngineReportFailure(DOMINO_CODE code) {
    if (code >= CODE_OK) {
        return code;
    }
    DOMINO_CODE expected_code = CODE_OK;
    atomic_compare_exchange_strong(&g_domino_lifecycle_error, &expected_code, code);
    code = dominoEngineGetLifecycleError();
    atomic_store(&g_domino_engine_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_FAILED);
    call_once(&g_domino_runtime_notifications_once, initNotificationSynchronization);
    if (g_domino_runtime_notifications_init_result != CODE_OK) {
        return code;
    }
    checkNotificationResult(mtx_lock(&g_domino_runtime_notifications.mutex), ERR_MUTEX_LOCK, "runtime notification mutex lock failed");
    if (g_domino_runtime_notifications.ready_flag) {
        dominoThreadQueueWakeAll(&g_domino_to_main_loop_queue);
        /* 唤醒可能正在等业务输出的客户端；错误通过独立首错状态读取。 */
        (void)dominoThreadQueueStopAndWake(&g_domino_from_main_loop_queue, DOMINO_THREAD_QUEUE_BOTH);
    }
    checkNotificationResult(mtx_unlock(&g_domino_runtime_notifications.mutex), ERR_MUTEX_UNLOCK, "runtime notification mutex unlock failed");
    return code;
}

static DOMINO_CODE failLifecycle(DOMINO_CODE code) {
    return dominoEngineReportFailure(code);
}

/** @brief 同步步骤完成时也观察工作线程发布的首错，禁止把异步失败覆盖为成功。 */
static DOMINO_CODE getInstanceResult(DOMINO_CODE result) {
    const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
    return failure_code != CODE_OK ? failure_code : result;
}

static DOMINO_CODE dominoEngineResetSessionState(void) {
    memset(&g_domino_engine_launch_config, 0, sizeof(g_domino_engine_launch_config));
    memset(g_domino_storage_path, 0, sizeof(g_domino_storage_path));
    memset(g_domino_storage_account_id, 0, sizeof(g_domino_storage_account_id));
    (void)snprintf(g_domino_storage_run_mode, sizeof(g_domino_storage_run_mode), "%s", "local");
    DominoEngineLifecycleState expected_state = DOMINO_ENGINE_LIFECYCLE_STOPPING;
    atomic_compare_exchange_strong(&g_domino_engine_lifecycle_state, &expected_state, DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED);
    return dominoEngineGetLifecycleError();
}

static DOMINO_CODE dominoEngineQueueModuleExit(void) {
    DOMINO_CODE code = dominoThreadQueueDestroy(&g_domino_to_main_loop_queue);
    if (code != CODE_OK) {
        return code;
    }
    return dominoThreadQueueDestroy(&g_domino_from_main_loop_queue);
}

static DOMINO_CODE initRuntimeNotifications(void) {
    call_once(&g_domino_runtime_notifications_once, initNotificationSynchronization);
    if (g_domino_runtime_notifications_init_result != CODE_OK) {
        return g_domino_runtime_notifications_init_result;
    }
    checkNotificationResult(mtx_lock(&g_domino_runtime_notifications.mutex), ERR_MUTEX_LOCK, "runtime notification mutex lock failed");
    g_domino_runtime_notifications.ready_flag = false;
    checkNotificationResult(mtx_unlock(&g_domino_runtime_notifications.mutex), ERR_MUTEX_UNLOCK, "runtime notification mutex unlock failed");
    return dominoEngineGetLifecycleError();
}

/** @brief 与异步错误通知串行撤下队列；调用前业务线程及主循环已 join。 */
static void retireRuntimeNotifications(void) {
    checkNotificationResult(mtx_lock(&g_domino_runtime_notifications.mutex), ERR_MUTEX_LOCK, "runtime notification mutex lock failed");
    g_domino_runtime_notifications.ready_flag = false;
    checkNotificationResult(mtx_unlock(&g_domino_runtime_notifications.mutex), ERR_MUTEX_UNLOCK, "runtime notification mutex unlock failed");
}

static DOMINO_CODE checkClientWorldAccess(void) {
    const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
    if (failure_code != CODE_OK) {
        return failure_code;
    }
    const DominoEngineLifecycleState state = atomic_load(&g_domino_engine_lifecycle_state);
    if (state == DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NOT_INITIALIZED, "world access requires an initialized engine");
    }
    if ((state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED && state != DOMINO_ENGINE_LIFECYCLE_RUNNING) || !dominoEngineIsClientThread()) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "operation is not allowed in the current lifecycle state or thread");
    }
    return CODE_OK;
}

DOMINO_CODE dominoEngineAuthorizeSleep(domino_human_id_t human_id, domino_building_id_t building_id) {
    DOMINO_CODE result = checkClientWorldAccess();
    return result == CODE_OK ? dominoHumanExecutionAuthorizeSleep(human_id, building_id) : result;
}

DOMINO_CODE dominoEngineSubmitSleep(domino_human_id_t human_id, domino_building_id_t building_id, uint8_t channel_index,
                                    domino_task_id_t* task_id_out_ptr) {
    DOMINO_CODE result = checkClientWorldAccess();
    return result == CODE_OK ? dominoHumanExecutionSubmitSleep(human_id, building_id, channel_index, task_id_out_ptr) : result;
}

DOMINO_CODE dominoEngineFreezeTime(void) {
    if (atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING || !dominoEngineIsClientThread()) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "lifecycle state or calling thread does not permit this operation"));
    }
    /* 客户端串行控制时钟；冻结无需唤醒已等待的线程。 */
    DOMINO_CODE result = getInstanceResult(dominoTimeModuleFreeze());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    domino_runtime_ms_t now_ms;
    result = getInstanceResult(dominoTimeGetRuntimeMs(&now_ms));
    return result == CODE_OK ? CODE_OK : failLifecycle(result);
}

/** @brief 恢复或退出状态先发布，再通知业务等待者；普通冻结不需要通知。 */
static void notifyBusinessThreads(void) {
    dominoHumanExecutionNotifyTimeChanged();
    dominoTimerNotifyTimeChanged();
}

DOMINO_CODE dominoEngineThawTime(void) {
    if (atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING || !dominoEngineIsClientThread()) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "lifecycle state or calling thread does not permit this operation"));
    }
    domino_runtime_ms_t now_ms;
    DOMINO_CODE result = getInstanceResult(dominoTimeGetRuntimeMs(&now_ms));
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = dominoTimeModuleThaw();
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    /* 主循环本身也可能停在冻结等待中，解冻后与其他业务线程一起唤醒。 */
    dominoThreadQueueWakeAll(&g_domino_to_main_loop_queue);
    notifyBusinessThreads();
    return dominoEngineGetLifecycleError();
}

DOMINO_CODE dominoEnginePushMsg(DominoEngineMessage* message_ptr) {
    DOMINO_CODE result = checkClientWorldAccess();
    if (result != CODE_OK) {
        return result;
    }
    if (atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "operation is not allowed in the current lifecycle state or thread");
    }
    if (!message_ptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "message pointer is null");
    }
    result = dominoThreadQueueProduce(&g_domino_to_main_loop_queue, message_ptr);
    if (result != CODE_OK) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(result, "failed to enqueue an engine message"));
    }
    return dominoEngineGetLifecycleError();
}

DOMINO_CODE dominoEnginePopMsg(DominoEngineMessage* message_out_ptr, bool blocking) {
    DOMINO_CODE result = checkClientWorldAccess();
    if (result != CODE_OK) {
        return result;
    }
    if (atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "operation is not allowed in the current lifecycle state or thread");
    }
    result = dominoThreadQueueConsume(&g_domino_from_main_loop_queue, message_out_ptr, blocking);
    const DOMINO_CODE failure_code = dominoEngineGetLifecycleError();
    return failure_code != CODE_OK ? failure_code : result;
}

DOMINO_CODE dominoLoopProduceToMain(DominoEngineMessage* message_ptr) {
    DOMINO_CODE result = dominoEngineGetLifecycleError();
    if (result != CODE_OK) {
        return result;
    }
    const DominoEngineLifecycleState state = atomic_load(&g_domino_engine_lifecycle_state);
    if (state != DOMINO_ENGINE_LIFECYCLE_RUNNING && state != DOMINO_ENGINE_LIFECYCLE_STOPPING) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "operation is not allowed in the current lifecycle state or thread");
    }
    if (!message_ptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "message pointer is null");
    }
    result = dominoThreadQueueProduce(&g_domino_from_main_loop_queue, message_ptr);
    if (result != CODE_OK) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(result, "failed to enqueue an engine message"));
    }
    return dominoEngineGetLifecycleError();
}

DOMINO_CODE dominoEngineInit(DominoEngineLaunchConfig launch_config) {
    if (g_domino_engine_lifecycle_state != DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_ALREADY_INITIALIZED, "engine initialization requires an uninitialized instance"));
    }
    if (launch_config.storage_path == nullptr) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "storage path pointer is null"));
    }
    if (launch_config.storage_path[0] == '\0') {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "storage path must name a non-root directory other than dot or dot-dot"));
    }
    size_t storage_path_len = strlen(launch_config.storage_path);
    while (storage_path_len > 0U && launch_config.storage_path[storage_path_len - 1U] == '/') {
        storage_path_len--;
    }
    if (storage_path_len == 0U) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "storage path must name a non-root directory other than dot or dot-dot"));
    }
    if (storage_path_len >= sizeof(g_domino_storage_path)) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_BUFFER_TOO_SMALL, "storage path exceeds the engine path buffer"));
    }
    /* 事务保存会重命名整个目录，末级必须是具体目录名。 */
    size_t base_index = storage_path_len;
    while (base_index > 0U && launch_config.storage_path[base_index - 1U] != '/') {
        base_index--;
    }
    size_t base_len = storage_path_len - base_index;
    if ((base_len == 1U && launch_config.storage_path[base_index] == '.') ||
        (base_len == 2U && launch_config.storage_path[base_index] == '.' && launch_config.storage_path[base_index + 1U] == '.')) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "storage path must name a non-root directory other than dot or dot-dot"));
    }

    /* 生命周期入口串行；独立日志线程仍可能发布故障，不能覆盖其 FAILED 状态。 */
    DominoEngineLifecycleState expected_state = DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED;
    if (!atomic_compare_exchange_strong(&g_domino_engine_lifecycle_state, &expected_state, DOMINO_ENGINE_LIFECYCLE_INITIALIZING)) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_ALREADY_INITIALIZED, "engine initialization requires an uninitialized instance"));
    }
    memcpy(g_domino_storage_path, launch_config.storage_path, storage_path_len);
    g_domino_storage_path[storage_path_len] = '\0';
    g_domino_engine_launch_config = launch_config;
    g_domino_engine_launch_config.storage_path = g_domino_storage_path;
    memset(g_domino_storage_account_id, 0, sizeof(g_domino_storage_account_id));
    (void)snprintf(g_domino_storage_run_mode, sizeof(g_domino_storage_run_mode), "%s", "local");

    DOMINO_CODE result = getInstanceResult(dominoLoggerModuleInit());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = getInstanceResult(dominoCommonModuleInit());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = getInstanceResult(dominoHostModuleInit());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = getInstanceResult(dominoNavModuleInitBefore());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = getInstanceResult(dominoSocialModuleInit());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = getInstanceResult(dominoHumanModuleInit());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    dominoTimeModuleInitBefore();
    result = getInstanceResult(dominoStorageModuleInit());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = getInstanceResult(dominoNavModuleInitAfter());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = getInstanceResult(dominoTimeModuleInitAfter());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    g_domino_client_thread = thrd_current();
    expected_state = DOMINO_ENGINE_LIFECYCLE_INITIALIZING;
    if (!atomic_compare_exchange_strong(&g_domino_engine_lifecycle_state, &expected_state, DOMINO_ENGINE_LIFECYCLE_INITIALIZED)) {
        return dominoEngineGetLifecycleError();
    }
    return CODE_OK;
}

/** @brief 客户端发布退出并唤醒全部业务线程，等待其结束后结算；投递目标保活到生产者退出。 */
static DOMINO_CODE stopRuntime(void) {
    atomic_store(&g_domino_stop_requested, true);
    DOMINO_CODE result = dominoTimeModuleFreeze();
    domino_runtime_ms_t now_ms;
    if (result == CODE_OK) {
        result = dominoTimeGetRuntimeMs(&now_ms);
    }
    dominoThreadQueueWakeAll(&g_domino_to_main_loop_queue);
    notifyBusinessThreads();
    if (result != CODE_OK) {
        return result;
    }
    /* 主循环错误路径也会通知业务模块，须先等其结束，才能回收其他线程资源。 */
    result = waitLifecycleCompletion(&g_domino_main_loop_completed);
    if (result != CODE_OK) {
        return result;
    }
    if (g_domino_main_loop_result != CODE_OK) {
        return g_domino_main_loop_result;
    }
    if (thrd_join(g_domino_main_loop_thread, nullptr) != thrd_success) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_THREAD_JOIN, "failed to join the main loop thread");
    }
    result = dominoHumanExecutionStop();
    if (result != CODE_OK) {
        return result;
    }
    result = dominoTimerModuleExit();
    if (result != CODE_OK) {
        return result;
    }
    return dominoHumanExecutionFinishStop(now_ms);
}

/** @brief 启动或运行失败均唤醒已启动的业务线程；调用时不与模块初始化／释放并发。 */
static DOMINO_CODE requestRuntimeFailure(DOMINO_CODE code) {
    code = dominoEngineReportFailure(code);
    dominoHumanExecutionRequestFailure(code);
    dominoTimerRequestFailure(code);
    return code;
}

/** @brief 失败只通知业务线程停止并发布主循环结束结果；残留资源保活到宿主结束进程。 */
static int completeRuntimeFailure(DOMINO_CODE code) {
    return completeMainLoop(requestRuntimeFailure(code));
}

static int dominoMainLoopThread(void* arg_ptr) {
    (void)arg_ptr;
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE result = dominoThreadQueueSnapshot(&g_domino_to_main_loop_queue, &epoch);
        if (result != CODE_OK) {
            return completeRuntimeFailure(result);
        }
        result = dominoEngineGetLifecycleError();
        if (result != CODE_OK) {
            return completeRuntimeFailure(result);
        }
        if (atomic_load(&g_domino_stop_requested)) {
            return completeMainLoop(CODE_OK);
        }
        if (dominoTimeModuleIsFrozen()) {
            result = dominoThreadQueueWait(&g_domino_to_main_loop_queue, epoch, nullptr);
            if (result != CODE_OK) {
                return completeRuntimeFailure(result);
            }
            continue;
        }
        uint32_t consumed_count = 0U;
        while (consumed_count < 32U) {
            result = dominoEngineGetLifecycleError();
            if (result != CODE_OK) {
                return completeRuntimeFailure(result);
            }
            DominoEngineMessage message;
            result = dominoThreadQueueConsume(&g_domino_to_main_loop_queue, &message, false);
            if (result == ERR_QUEUE_EMPTY) {
                break;
            }
            if (result != CODE_OK) {
                return completeRuntimeFailure(result);
            }
            consumed_count++;
            /* 非法消息在分发入口记录；拒绝本条消息后继续消费。 */
            (void)dominoDispatchMessage(&message);
        }
        if (consumed_count == 32U) {
            continue;
        }
        /* 本轮采样后已检查状态和消息；恢复、退出及失败均在发布后唤醒。 */
        result = dominoThreadQueueWait(&g_domino_to_main_loop_queue, epoch, nullptr);
        if (result != CODE_OK && result != ERR_QUEUE_CONSUMER_STOPPED && result != ERR_QUEUE_PRODUCER_STOPPED) {
            return completeRuntimeFailure(result);
        }
    }
}

DOMINO_CODE dominoEngineRun(DominoEngineClientData* client_data_out_ptr) {
    const DominoEngineLifecycleState state = atomic_load(&g_domino_engine_lifecycle_state);
    if (state == DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_NOT_INITIALIZED, "engine has not been initialized"));
    }
    if (state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED || !dominoEngineIsClientThread()) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "lifecycle state or calling thread does not permit this operation"));
    }
    DOMINO_CODE result = initRuntimeNotifications();
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = dominoThreadQueueInit(&g_domino_to_main_loop_queue, sizeof(DominoEngineMessage), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                   DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = dominoThreadQueueInit(&g_domino_from_main_loop_queue, sizeof(DominoEngineMessage), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                   DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    atomic_store(&g_domino_stop_requested, false);
    atomic_store(&g_domino_main_loop_completed, false);
    DominoEngineLifecycleState expected_state = DOMINO_ENGINE_LIFECYCLE_INITIALIZED;
    if (!atomic_compare_exchange_strong(&g_domino_engine_lifecycle_state, &expected_state, DOMINO_ENGINE_LIFECYCLE_STARTING)) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "lifecycle state or calling thread does not permit this operation"));
    }
    checkNotificationResult(mtx_lock(&g_domino_runtime_notifications.mutex), ERR_MUTEX_LOCK, "runtime notification mutex lock failed");
    g_domino_runtime_notifications.ready_flag = true;
    result = dominoEngineGetLifecycleError();
    checkNotificationResult(mtx_unlock(&g_domino_runtime_notifications.mutex), ERR_MUTEX_UNLOCK, "runtime notification mutex unlock failed");
    if (result != CODE_OK) {
        return result;
    }
    /* 时钟保持冻结，客户端直接启动业务模块；主循环最后创建，避免失败通知访问半初始化资源。 */
    result = dominoHumanExecutionStart();
    if (result != CODE_OK) {
        return requestRuntimeFailure(result);
    }
    result = dominoTimerModuleInit();
    if (result != CODE_OK) {
        return requestRuntimeFailure(result);
    }
    if (thrd_create(&g_domino_main_loop_thread, dominoMainLoopThread, nullptr) != thrd_success) {
        return requestRuntimeFailure(DOMINO_LOG_DIRECT_ERROR(ERR_THREAD_CREATE, "failed to create the main loop thread"));
    }
    /* 首次恢复同样属于本次实例的成功条件，不保留失败后再次 Thaw 的路径。 */
    expected_state = DOMINO_ENGINE_LIFECYCLE_STARTING;
    if (!atomic_compare_exchange_strong(&g_domino_engine_lifecycle_state, &expected_state, DOMINO_ENGINE_LIFECYCLE_RUNNING)) {
        return requestRuntimeFailure(DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "engine cannot enter the running state"));
    }
    result = dominoEngineThawTime();
    if (result != CODE_OK) {
        return requestRuntimeFailure(result);
    }
    if (client_data_out_ptr) {
        client_data_out_ptr->game_date_time_ns = dominoGetRuntimeDateTimeBase();
    }
    return CODE_OK;
}

DOMINO_CODE dominoEngineExit(void) {
    const DominoEngineLifecycleState state = atomic_load(&g_domino_engine_lifecycle_state);
    if (state == DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_NOT_INITIALIZED, "engine has not been initialized"));
    }
    if ((state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED && state != DOMINO_ENGINE_LIFECYCLE_RUNNING) || !dominoEngineIsClientThread()) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "lifecycle state or calling thread does not permit this operation"));
    }
    DOMINO_CODE result;
    DominoEngineLifecycleState expected_state = state;
    if (!atomic_compare_exchange_strong(&g_domino_engine_lifecycle_state, &expected_state, DOMINO_ENGINE_LIFECYCLE_STOPPING)) {
        return failLifecycle(DOMINO_LOG_DIRECT_ERROR(ERR_GAME_STATE_INVALID, "lifecycle state or calling thread does not permit this operation"));
    }
    /* RUNNING 已保证主循环和队列完整创建；INITIALIZED 尚未创建这些资源。 */
    if (state == DOMINO_ENGINE_LIFECYCLE_RUNNING) {
        result = getInstanceResult(stopRuntime());
        if (result != CODE_OK) {
            return failLifecycle(result);
        }
        retireRuntimeNotifications();
        result = getInstanceResult(dominoEngineQueueModuleExit());
        if (result != CODE_OK) {
            return failLifecycle(result);
        }
    }
    result = getInstanceResult(dominoNavModuleExitBefore());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = getInstanceResult(dominoStorageModuleExit());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    dominoHumanModuleExit();
    dominoSocialModuleExit();
    dominoNavModuleExitAfter();
    dominoHostModuleExit();
    dominoCommonModuleExit();
    result = getInstanceResult(dominoLoggerModuleExit());
    if (result != CODE_OK) {
        return failLifecycle(result);
    }
    result = dominoEngineGetLifecycleError();
    if (result != CODE_OK) {
        return result;
    }
    return dominoEngineResetSessionState();
}
