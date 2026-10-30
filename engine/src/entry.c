#include "entry.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include <threads.h>

#include "common/entry.h"
#include "dispatch/entry.h"
#include "domino_engine.h"
#include "domino_shared_common.h"
#include "domino_shared_error_codes.h"
#include "domino_shared_queue.h"
#include "game_timer/entry.h"
#include "host/entry.h"
#include "human/entry.h"
#include "human/executor.h"
#include "logger/entry.h"
#include "nav/entry.h"
#include "social/entry.h"
#include "storage/entry.h"
#include "time/entry.h"

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
    DOMINO_ENGINE_LIFECYCLE_STOPPED,
    DOMINO_ENGINE_LIFECYCLE_DESTROYING,
} DominoEngineLifecycleState;

static _Atomic DominoEngineLifecycleState g_domino_engine_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED;

DominoEngineLaunchConfig g_domino_engine_launch_config = {};

bool dominoEngineIsClientThread(void) {
    return thrd_equal(thrd_current(), g_domino_client_thread) != 0;
}

bool dominoEngineIsStopping(void) {
    return atomic_load(&g_domino_engine_lifecycle_state) == DOMINO_ENGINE_LIFECYCLE_STOPPING;
}

bool dominoEngineIsRunning(void) {
    return atomic_load(&g_domino_engine_lifecycle_state) == DOMINO_ENGINE_LIFECYCLE_RUNNING;
}

/** @brief 只校验访问资格；请求边界负责记录一次拒绝结果。 */
static DOMINO_CODE checkClientWorldAccess(void) {
    const DominoEngineLifecycleState state = atomic_load(&g_domino_engine_lifecycle_state);
    if (state == DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return ERR_NOT_INITIALIZED;
    }
    if ((state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED && state != DOMINO_ENGINE_LIFECYCLE_RUNNING) || !dominoEngineIsClientThread()) {
        return ERR_GAME_STATE_INVALID;
    }
    return CODE_OK;
}

DOMINO_CODE dominoEngineSubmitSleep(domino_human_id_t human_id, uint8_t channel_index, domino_task_id_t* task_id_out_ptr) {
    DOMINO_CODE code = checkClientWorldAccess();
    if (code == CODE_OK) {
        code = dominoHumanExecutionSubmitSleep(human_id, channel_index, task_id_out_ptr);
    }
    if (code != CODE_OK) {
        if (atomic_load(&g_domino_engine_lifecycle_state) == DOMINO_ENGINE_LIFECYCLE_RUNNING) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_ERROR,
                              "sleep submission rejected human=%" PRIu32 " channel=%u (%s, code=%d)", human_id, (unsigned)channel_index,
                              dominoErrorCodeToString(code), (int)code);
        } else {
            DOMINO_PRINT_MESSAGE("sleep submission rejected human=%" PRIu32 " channel=%u (%s, code=%d)", human_id, (unsigned)channel_index,
                                 dominoErrorCodeToString(code), (int)code);
        }
    }
    return code;
}

void dominoEngineFreezeTime(void) {
    if (atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "freeze requires the running engine's client thread");
    }
    /* 普通冻结不等待当前业务批次，日志线程始终保持运行。 */
    dominoTimeModuleFreeze();
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
}

/** @brief 恢复或退出状态先发布，再通知业务等待者；普通冻结不需要通知。 */
static void notifyBusinessThreads(void) {
    dominoHumanExecutionNotifyTimeChanged();
    dominoGameTimerNotifyTimeChanged();
}

void dominoEngineThawTime(void) {
    if (atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "thaw requires the running engine's client thread");
    }
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    dominoTimeModuleThaw();
    dominoThreadQueueWakeAll(&g_domino_to_main_loop_queue);
    notifyBusinessThreads();
}

DOMINO_CODE dominoEnginePushMsg(DominoEngineMessage* message_ptr) {
    DOMINO_CODE code = checkClientWorldAccess();
    if (code == CODE_OK && atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING) {
        code = ERR_GAME_STATE_INVALID;
    }
    if (code == CODE_OK && message_ptr == nullptr) {
        code = ERR_NULL_POINTER;
    }
    if (code != CODE_OK) {
        if (atomic_load(&g_domino_engine_lifecycle_state) == DOMINO_ENGINE_LIFECYCLE_RUNNING) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "message submission rejected (%s, code=%d)",
                              dominoErrorCodeToString(code), (int)code);
        } else {
            DOMINO_PRINT_MESSAGE("message submission rejected (%s, code=%d)", dominoErrorCodeToString(code), (int)code);
        }
        return code;
    }
    const DOMINO_CODE result = dominoThreadQueueProduce(&g_domino_to_main_loop_queue, message_ptr, false);
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to enqueue an engine message");
    }
    return CODE_OK;
}

DOMINO_CODE dominoEnginePopMsg(DominoEngineMessage* message_out_ptr, bool blocking) {
    DOMINO_CODE code = checkClientWorldAccess();
    if (code == CODE_OK && atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING) {
        code = ERR_GAME_STATE_INVALID;
    }
    if (code == CODE_OK && message_out_ptr == nullptr) {
        code = ERR_NULL_POINTER;
    }
    if (code != CODE_OK) {
        if (atomic_load(&g_domino_engine_lifecycle_state) == DOMINO_ENGINE_LIFECYCLE_RUNNING) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "message consumption rejected (%s, code=%d)",
                              dominoErrorCodeToString(code), (int)code);
        } else {
            DOMINO_PRINT_MESSAGE("message consumption rejected (%s, code=%d)", dominoErrorCodeToString(code), (int)code);
        }
        return code;
    }
    return dominoThreadQueueConsume(&g_domino_from_main_loop_queue, message_out_ptr, blocking);
}

void dominoLoopProduceToMain(DominoEngineMessage* message_ptr) {
    const DOMINO_CODE code = dominoThreadQueueProduce(&g_domino_from_main_loop_queue, message_ptr, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "failed to enqueue a client message");
    }
}

void dominoEngineInit(DominoEngineLaunchConfig launch_config) {
    /* 入口一次完成重复调用检查及状态切换；参数错误直接退出，不返回半初始化实例。 */
    DominoEngineLifecycleState expected_state = DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED;
    if (!atomic_compare_exchange_strong(&g_domino_engine_lifecycle_state, &expected_state, DOMINO_ENGINE_LIFECYCLE_INITIALIZING)) {
        DOMINO_EXIT_WITH_ERROR(ERR_ALREADY_INITIALIZED, "engine initialization requires an uninitialized instance");
    }
    if (launch_config.storage_path == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "storage path pointer is null");
    }
    if (launch_config.storage_path[0] == '\0') {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "storage path must name a non-root directory other than dot or dot-dot");
    }
    size_t storage_path_len = strlen(launch_config.storage_path);
    while (storage_path_len > 0U && launch_config.storage_path[storage_path_len - 1U] == '/') {
        storage_path_len--;
    }
    if (storage_path_len == 0U) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "storage path must name a non-root directory other than dot or dot-dot");
    }
    if (storage_path_len >= sizeof(g_domino_storage_path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "storage path exceeds the engine path buffer");
    }
    /* 事务保存会重命名整个目录，末级必须是具体目录名。 */
    size_t base_index = storage_path_len;
    while (base_index > 0U && launch_config.storage_path[base_index - 1U] != '/') {
        base_index--;
    }
    size_t base_len = storage_path_len - base_index;
    if ((base_len == 1U && launch_config.storage_path[base_index] == '.') ||
        (base_len == 2U && launch_config.storage_path[base_index] == '.' && launch_config.storage_path[base_index + 1U] == '.')) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "storage path must name a non-root directory other than dot or dot-dot");
    }

    memcpy(g_domino_storage_path, launch_config.storage_path, storage_path_len);
    g_domino_storage_path[storage_path_len] = '\0';
    g_domino_engine_launch_config = launch_config;
    g_domino_engine_launch_config.storage_path = g_domino_storage_path;

    dominoLoggerModuleInit();
    dominoThreadQueueInit(&g_domino_to_main_loop_queue, sizeof(DominoEngineMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                          DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    dominoThreadQueueInit(&g_domino_from_main_loop_queue, sizeof(DominoEngineMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                          DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    dominoCommonModuleInit();
    dominoTimeModuleInit();
    dominoHostModuleInit();
    dominoNavModuleInit();
    dominoSocialModuleInit();
    dominoHumanModuleInit();
    dominoGameTimerModuleInit();
    dominoStorageModuleInit();
    dominoNavModuleInitAfter();
    g_domino_client_thread = thrd_current();
    atomic_store(&g_domino_engine_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_INITIALIZED);
}

static int dominoMainLoopThread(void* arg_ptr) {
    (void)arg_ptr;
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(&g_domino_to_main_loop_queue, &epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "failed to snapshot the main loop queue");
        }
        if (dominoEngineIsStopping()) {
            return 0;
        }
        const uint64_t freeze_epoch = dominoTimeModuleGetFreezeEpoch();
        if (freeze_epoch != 0U) {
            dominoTimeModuleWait(freeze_epoch);
            continue;
        }
        uint32_t consumed_count = 0U;
        while (consumed_count < 32U) {
            DominoEngineMessage message;
            code = dominoThreadQueueConsume(&g_domino_to_main_loop_queue, &message, false);
            if (code == ERR_QUEUE_EMPTY) {
                break;
            }
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "failed to consume a main loop message");
            }
            consumed_count++;
            /* 分发入口记录并拒绝非法业务消息，继续处理下一条。 */
            (void)dominoDispatchMessage(&message);
        }
        if (consumed_count == 32U) {
            continue;
        }
        code = dominoThreadQueueWait(&g_domino_to_main_loop_queue, epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "failed to wait for main loop work");
        }
    }
}

void dominoEngineStart(DominoEngineClientData* client_data_out_ptr) {
    const DominoEngineLifecycleState state = atomic_load(&g_domino_engine_lifecycle_state);
    if (state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "start requires an initialized engine and its client thread");
    }
    atomic_store(&g_domino_engine_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_STARTING);
    /* 时钟保持冻结，等所有消费者就绪后再恢复。 */
    dominoLoggerModuleStart();
    dominoHumanModuleStart();
    dominoGameTimerModuleStart();
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_domino_main_loop_thread, dominoMainLoopThread, nullptr), ERR_THREAD_CREATE,
                                  "failed to create the main loop thread");
    atomic_store(&g_domino_engine_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_RUNNING);
    dominoEngineThawTime();
    if (client_data_out_ptr != nullptr) {
        client_data_out_ptr->game_date_time_ns = dominoGetRuntimeDateTimeBase();
    }
    DOMINO_PRINT_MESSAGE("engine started at runtime_ns=%" PRIu64, dominoGetRuntimeDateTimeBase());
}

void dominoEngineStop(void) {
    const DominoEngineLifecycleState state = atomic_load(&g_domino_engine_lifecycle_state);
    if ((state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED && state != DOMINO_ENGINE_LIFECYCLE_RUNNING) || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "stop requires an initialized or running engine and its client thread");
    }
    /* STOPPING 同时发布退出请求，保持到业务线程 join、人物结算和日志排空完成。 */
    atomic_store(&g_domino_engine_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_STOPPING);
    if (state == DOMINO_ENGINE_LIFECYCLE_RUNNING) {
        dominoTimeModuleFreeze();
        domino_runtime_ms_t now_ms;
        dominoTimeGetRuntimeMs(&now_ms);
        dominoTimeModuleWakeAll();
        dominoThreadQueueWakeAll(&g_domino_to_main_loop_queue);
        notifyBusinessThreads();
        DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_domino_main_loop_thread, nullptr), ERR_THREAD_JOIN, "failed to join the main loop thread");
        dominoHumanModuleStop();
        dominoGameTimerModuleStop();
        dominoHumanExecutionSettle(now_ms);
    }
    /* 仅 Init 时不调用业务线程 Stop，不结算人物；logger 仍关闭入队并刷新输出。 */
    dominoLoggerModuleStop();
    atomic_store(&g_domino_engine_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_STOPPED);
}

void dominoEngineDestroy(void) {
    if (atomic_load(&g_domino_engine_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_STOPPED || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "destroy requires a stopped engine and its client thread");
    }
    atomic_store(&g_domino_engine_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_DESTROYING);
    dominoGameTimerModuleDestroy();
    dominoThreadQueueDestroy(&g_domino_to_main_loop_queue);
    dominoThreadQueueDestroy(&g_domino_from_main_loop_queue);
    dominoNavModuleDestroyBefore();
    dominoStorageModuleDestroy();
    dominoHumanModuleDestroy();
    dominoSocialModuleDestroy();
    dominoNavModuleDestroy();
    dominoHostModuleDestroy();
    dominoCommonModuleDestroy();
    /* 存档已保留退出时刻；现在才清空本实例的内存时钟。 */
    dominoTimeModuleDestroy();
    dominoLoggerModuleDestroy();
    memset(&g_domino_client_thread, 0, sizeof(g_domino_client_thread));
    memset(&g_domino_main_loop_thread, 0, sizeof(g_domino_main_loop_thread));
    memset(&g_domino_engine_launch_config, 0, sizeof(g_domino_engine_launch_config));
    memset(g_domino_storage_path, 0, sizeof(g_domino_storage_path));
    memset(g_domino_storage_account_id, 0, sizeof(g_domino_storage_account_id));
    memset(g_domino_storage_run_mode, 0, sizeof(g_domino_storage_run_mode));
    memcpy(g_domino_storage_run_mode, "local", sizeof("local"));
    atomic_store(&g_domino_engine_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED);
}
