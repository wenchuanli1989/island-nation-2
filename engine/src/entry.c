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
#include "logger/entry.h"
#include "nav/entry.h"
#include "social/entry.h"
#include "storage/entry.h"
#include "time/entry.h"

char g_domino_account_id[DOMINO_ACCOUNT_ID_SIZE] = "";
char g_domino_run_mode[DOMINO_RUN_MODE_SIZE] = "local";

static char g_storage_path[DOMINO_STORAGE_PATH_MAX] = "";

static thrd_t g_client_thread;
static thrd_t g_main_loop_thread;

static DominoThreadQueue g_main_loop_inbox;  ///< 客户端 → 主循环。
static DominoThreadQueue g_client_inbox;     ///< 引擎 → 客户端。

enum { MAIN_LOOP_BATCH_MAX = 32U };

typedef enum DOMINO_ENGINE_LIFECYCLE_STATE {
    DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED = 0,
    DOMINO_ENGINE_LIFECYCLE_INITIALIZING,
    DOMINO_ENGINE_LIFECYCLE_INITIALIZED,
    DOMINO_ENGINE_LIFECYCLE_STARTING,
    DOMINO_ENGINE_LIFECYCLE_RUNNING,
    DOMINO_ENGINE_LIFECYCLE_STOPPING,
    DOMINO_ENGINE_LIFECYCLE_STOPPED,
    DOMINO_ENGINE_LIFECYCLE_DESTROYING,
} DOMINO_ENGINE_LIFECYCLE_STATE;

static _Atomic DOMINO_ENGINE_LIFECYCLE_STATE g_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED;

DominoEngineLaunchConfig g_domino_engine_launch_config = {};

bool dominoEngineIsClientThread(void) {
    return thrd_equal(thrd_current(), g_client_thread) != 0;
}

bool dominoEngineIsStopping(void) {
    return atomic_load(&g_lifecycle_state) == DOMINO_ENGINE_LIFECYCLE_STOPPING;
}

/** @brief 客户端消息接口的访问资格：RUNNING 且为客户端线程；拒绝结果由调用方记录。 */
static DOMINO_CODE checkClientAccess(void) {
    const DOMINO_ENGINE_LIFECYCLE_STATE state = atomic_load(&g_lifecycle_state);
    if (state == DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return ERR_NOT_INITIALIZED;
    }
    if (state != DOMINO_ENGINE_LIFECYCLE_RUNNING || !dominoEngineIsClientThread()) {
        return ERR_GAME_STATE_INVALID;
    }
    return CODE_OK;
}

void dominoEngineFreezeTime(void) {
    if (atomic_load(&g_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "freeze requires the running engine's client thread");
    }
    // 普通冻结不通知也不等待业务线程。读取一次业务时间，校验冻结时刻仍在 32 位毫秒范围内。
    dominoTimeModuleFreeze();
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
}

/** @brief 解冻或退出状态发布后通知业务线程重新检查；普通冻结不需要通知。 */
static void notifyBusinessThreads(void) {
    dominoHumanModuleNotifyTimeChanged();
    dominoGameTimerNotifyTimeChanged();
}

void dominoEngineThawTime(void) {
    if (atomic_load(&g_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_RUNNING || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "thaw requires the running engine's client thread");
    }
    // 解冻前校验冻结基准仍在 32 位业务毫秒范围内。
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    dominoTimeModuleThaw();
    dominoThreadQueueWakeAll(&g_main_loop_inbox);
    notifyBusinessThreads();
}

DOMINO_CODE dominoEngineSendMessage(DominoEngineMessage* message_ptr) {
    DOMINO_CODE code = checkClientAccess();
    if (code == CODE_OK && message_ptr == nullptr) {
        code = ERR_NULL_POINTER;
    }
    if (code != CODE_OK) {
        if (atomic_load(&g_lifecycle_state) == DOMINO_ENGINE_LIFECYCLE_RUNNING) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "message submission rejected (%s, code=%d)",
                              dominoErrorCodeToString(code), (int)code);
        } else {
            DOMINO_PRINT_MESSAGE("message submission rejected (%s, code=%d)", dominoErrorCodeToString(code), (int)code);
        }
        return code;
    }
    const DOMINO_CODE result = dominoThreadQueueProduce(&g_main_loop_inbox, message_ptr, false);
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to enqueue an engine message");
    }
    return CODE_OK;
}

DOMINO_CODE dominoEngineReceiveMessage(DominoEngineMessage* message_out_ptr, bool blocking) {
    DOMINO_CODE code = checkClientAccess();
    if (code == CODE_OK && message_out_ptr == nullptr) {
        code = ERR_NULL_POINTER;
    }
    if (code != CODE_OK) {
        if (atomic_load(&g_lifecycle_state) == DOMINO_ENGINE_LIFECYCLE_RUNNING) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "message consumption rejected (%s, code=%d)",
                              dominoErrorCodeToString(code), (int)code);
        } else {
            DOMINO_PRINT_MESSAGE("message consumption rejected (%s, code=%d)", dominoErrorCodeToString(code), (int)code);
        }
        return code;
    }
    return dominoThreadQueueConsume(&g_client_inbox, message_out_ptr, blocking);
}

void dominoEngineSendToClient(DominoEngineMessage* message_ptr) {
    const DOMINO_CODE code = dominoThreadQueueProduce(&g_client_inbox, message_ptr, false);
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "failed to enqueue a client message");
    }
}

void dominoEngineInit(DominoEngineLaunchConfig launch_config) {
    DOMINO_ENGINE_LIFECYCLE_STATE expected_state = DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED;
    if (!atomic_compare_exchange_strong(&g_lifecycle_state, &expected_state, DOMINO_ENGINE_LIFECYCLE_INITIALIZING)) {
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
    if (storage_path_len >= sizeof(g_storage_path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "storage path exceeds the engine path buffer");
    }
    // 事务保存要重命名整个存档目录，末级必须是具体目录名。
    size_t base_index = storage_path_len;
    while (base_index > 0U && launch_config.storage_path[base_index - 1U] != '/') {
        base_index--;
    }
    size_t base_len = storage_path_len - base_index;
    if ((base_len == 1U && launch_config.storage_path[base_index] == '.') ||
        (base_len == 2U && launch_config.storage_path[base_index] == '.' && launch_config.storage_path[base_index + 1U] == '.')) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "storage path must name a non-root directory other than dot or dot-dot");
    }

    memcpy(g_storage_path, launch_config.storage_path, storage_path_len);
    g_storage_path[storage_path_len] = '\0';
    g_domino_engine_launch_config = launch_config;
    g_domino_engine_launch_config.storage_path = g_storage_path;

    dominoLoggerModuleInit();
    dominoThreadQueueInit(&g_main_loop_inbox, sizeof(DominoEngineMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    dominoThreadQueueInit(&g_client_inbox, sizeof(DominoEngineMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY, DOMINO_THREAD_QUEUE_MAX_CAPACITY);
    dominoCommonModuleInit();
    dominoTimeModuleInit();
    dominoHostModuleInit();
    dominoNavModuleInit();
    dominoSocialModuleInit();
    dominoHumanModuleInit();
    dominoGameTimerModuleInit();
    dominoStorageModuleInit();
    dominoNavModuleInitAfter();
    dominoHumanModuleInitAfter();
    g_client_thread = thrd_current();
    atomic_store(&g_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_INITIALIZED);
}

/** @brief Snapshot → 退出／冻结检查 → 至多处理一批客户端消息 → 没有更多消息时按快照等待。 */
static int mainLoopThreadMain(void* arg_ptr) {
    (void)arg_ptr;
    for (;;) {
        uint64_t epoch;
        DOMINO_CODE code = dominoThreadQueueSnapshot(&g_main_loop_inbox, &epoch);
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
        while (consumed_count < MAIN_LOOP_BATCH_MAX) {
            DominoEngineMessage message;
            code = dominoThreadQueueConsume(&g_main_loop_inbox, &message, false);
            if (code == ERR_QUEUE_EMPTY) {
                break;
            }
            if (code != CODE_OK) {
                DOMINO_EXIT_WITH_ERROR(code, "failed to consume a main loop message");
            }
            consumed_count++;
            // 非法消息由分派入口记录并拒绝，继续处理下一条。
            (void)dominoDispatchClientMessage(&message);
        }
        if (consumed_count == MAIN_LOOP_BATCH_MAX) {
            continue;
        }
        code = dominoThreadQueueWait(&g_main_loop_inbox, epoch);
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "failed to wait for main loop work");
        }
    }
}

void dominoEngineStart(DominoEngineClientData* client_data_out_ptr) {
    const DOMINO_ENGINE_LIFECYCLE_STATE state = atomic_load(&g_lifecycle_state);
    if (state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "start requires an initialized engine and its client thread");
    }
    atomic_store(&g_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_STARTING);
    // 时钟保持冻结，全部线程就绪后再解冻。
    dominoLoggerModuleStart();
    dominoHumanModuleStart();
    dominoGameTimerModuleStart();
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&g_main_loop_thread, mainLoopThreadMain, nullptr), ERR_THREAD_CREATE,
                                  "failed to create the main loop thread");
    atomic_store(&g_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_RUNNING);
    dominoEngineThawTime();
    if (client_data_out_ptr != nullptr) {
        client_data_out_ptr->runtime_base_ns = dominoTimeGetRuntimeBaseNs();
    }
    DOMINO_PRINT_MESSAGE("engine started at runtime_ns=%" PRIu64, dominoTimeGetRuntimeBaseNs());
}

void dominoEngineStop(void) {
    const DOMINO_ENGINE_LIFECYCLE_STATE state = atomic_load(&g_lifecycle_state);
    if ((state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED && state != DOMINO_ENGINE_LIFECYCLE_RUNNING) || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "stop requires an initialized or running engine and its client thread");
    }
    // STOPPING 同时是业务线程的退出请求。
    atomic_store(&g_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_STOPPING);
    if (state == DOMINO_ENGINE_LIFECYCLE_RUNNING) {
        dominoTimeModuleFreeze();
        domino_runtime_ms_t now_ms;
        dominoTimeGetRuntimeMs(&now_ms);
        dominoTimeModuleWakeAll();
        dominoThreadQueueWakeAll(&g_main_loop_inbox);
        notifyBusinessThreads();
        DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(g_main_loop_thread, nullptr), ERR_THREAD_JOIN, "failed to join the main loop thread");
        dominoHumanModuleStop();
        dominoGameTimerModuleStop();
        // 业务线程全部结束后按停钟时刻统一结算；logger 仍在运行，可记录结算日志。
        dominoHumanModuleSettle(now_ms);
    }
    dominoLoggerModuleStop();
    atomic_store(&g_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_STOPPED);
}

void dominoEngineDestroy(void) {
    if (atomic_load(&g_lifecycle_state) != DOMINO_ENGINE_LIFECYCLE_STOPPED || !dominoEngineIsClientThread()) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "destroy requires a stopped engine and its client thread");
    }
    atomic_store(&g_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_DESTROYING);
    dominoGameTimerModuleDestroy();
    dominoThreadQueueDestroy(&g_main_loop_inbox);
    dominoThreadQueueDestroy(&g_client_inbox);
    dominoNavModuleDestroyBefore();
    dominoStorageModuleDestroy();
    dominoHumanModuleDestroy();
    dominoSocialModuleDestroy();
    dominoNavModuleDestroy();
    dominoHostModuleDestroy();
    dominoCommonModuleDestroy();
    // 存档已保存累计时间，此时才清空内存时钟。
    dominoTimeModuleDestroy();
    dominoLoggerModuleDestroy();
    memset(&g_client_thread, 0, sizeof(g_client_thread));
    memset(&g_main_loop_thread, 0, sizeof(g_main_loop_thread));
    memset(&g_domino_engine_launch_config, 0, sizeof(g_domino_engine_launch_config));
    memset(g_storage_path, 0, sizeof(g_storage_path));
    memset(g_domino_account_id, 0, sizeof(g_domino_account_id));
    memset(g_domino_run_mode, 0, sizeof(g_domino_run_mode));
    memcpy(g_domino_run_mode, "local", sizeof("local"));
    atomic_store(&g_lifecycle_state, DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED);
}
