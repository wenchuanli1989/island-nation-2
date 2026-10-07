#include "entry.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "common/entry.h"
#include "dispatch/entry.h"
#include "domino_engine.h"
#include "domino_shared_error_codes.h"
#include "host/entry.h"
#include "human/entry.h"
#include "logger/entry.h"
#include "nav/entry.h"
#include "queue/thread_queue.h"
#include "social/entry.h"
#include "storage/entry.h"
#include "time/entry.h"

char g_domino_storage_account_id[DOMINO_STORAGE_ACCOUNT_ID_MAX] = "";
char g_domino_storage_run_mode[DOMINO_STORAGE_RUN_MODE_MAX] = "local";

static bool g_domino_main_loop_thread_started = false;
static char g_domino_storage_path[DOMINO_STORAGE_PATH_MAX] = "";

static thrd_t g_domino_client_thread;
static thrd_t g_domino_human_behavior_planning_thread;
static thrd_t g_domino_main_loop_thread;

static DominoThreadQueue g_domino_to_main_loop_queue;
static DominoThreadQueue g_domino_from_main_loop_queue;

typedef enum DominoEngineLifecycleState {
    DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED = 0,
    DOMINO_ENGINE_LIFECYCLE_INITIALIZING,
    DOMINO_ENGINE_LIFECYCLE_INITIALIZED,
    DOMINO_ENGINE_LIFECYCLE_RUNNING,
    DOMINO_ENGINE_LIFECYCLE_STOPPING,
} DominoEngineLifecycleState;

typedef enum DominoEngineExitProgress {
    DOMINO_ENGINE_EXIT_PROGRESS_NONE = 0,
    DOMINO_ENGINE_EXIT_PROGRESS_THREADS_STOPPED,
    DOMINO_ENGINE_EXIT_PROGRESS_TIME_SETTLED,
} DominoEngineExitProgress;

static DominoEngineLifecycleState g_domino_engine_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED;
/* 退出失败后保留已完成阶段，重试时避免重复停止线程或累计时间。 */
static DominoEngineExitProgress g_domino_engine_exit_progress = DOMINO_ENGINE_EXIT_PROGRESS_NONE;

_Atomic bool g_domino_human_behavior_planning_stop = true;

DominoEngineLaunchConfig g_domino_engine_launch_config = {};

bool dominoEngineIsClientThread(void) {
    return (g_domino_engine_lifecycle_state == DOMINO_ENGINE_LIFECYCLE_INITIALIZED ||
            g_domino_engine_lifecycle_state == DOMINO_ENGINE_LIFECYCLE_RUNNING) &&
           thrd_equal(thrd_current(), g_domino_client_thread) != 0;
}

typedef enum DominoEngineInitStage {
    DOMINO_ENGINE_INIT_STAGE_NONE = 0,
    DOMINO_ENGINE_INIT_STAGE_LOGGER,
    DOMINO_ENGINE_INIT_STAGE_COMMON,
    DOMINO_ENGINE_INIT_STAGE_HOST,
    DOMINO_ENGINE_INIT_STAGE_NAV,
    DOMINO_ENGINE_INIT_STAGE_SOCIAL,
    DOMINO_ENGINE_INIT_STAGE_HUMAN,
} DominoEngineInitStage;

static void dominoEngineResetSessionState(void) {
    memset(&g_domino_engine_launch_config, 0, sizeof(g_domino_engine_launch_config));
    memset(g_domino_storage_path, 0, sizeof(g_domino_storage_path));
    memset(g_domino_storage_account_id, 0, sizeof(g_domino_storage_account_id));
    (void)snprintf(g_domino_storage_run_mode, sizeof(g_domino_storage_run_mode), "%s", "local");
    g_domino_main_loop_thread_started = false;
    g_domino_engine_exit_progress = DOMINO_ENGINE_EXIT_PROGRESS_NONE;
    g_domino_engine_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED;
}

static void dominoEngineRollbackInit(DominoEngineInitStage stage) {
    dominoTimeModuleInitBefore();
    if (stage >= DOMINO_ENGINE_INIT_STAGE_HUMAN) {
        dominoHumanModuleExit();
    }
    if (stage >= DOMINO_ENGINE_INIT_STAGE_SOCIAL) {
        dominoSocialModuleExit();
    }
    if (stage >= DOMINO_ENGINE_INIT_STAGE_NAV) {
        dominoNavModuleExitAfter();
    }
    if (stage >= DOMINO_ENGINE_INIT_STAGE_HOST) {
        dominoHostModuleExit();
    }
    if (stage >= DOMINO_ENGINE_INIT_STAGE_COMMON) {
        dominoCommonModuleExit();
    }
    if (stage >= DOMINO_ENGINE_INIT_STAGE_LOGGER) {
        dominoLoggerModuleExit();
    }
    dominoEngineResetSessionState();
}

static void dominoEngineQueueModuleExit(void) {
    dominoThreadQueueDestroy(&g_domino_to_main_loop_queue);
    dominoThreadQueueDestroy(&g_domino_from_main_loop_queue);
}

static DOMINO_CODE dominoEngineShutdownMainLoopThread(void) {
    if (!g_domino_main_loop_thread_started) {
        return CODE_OK;
    }

    /* 先禁止队列操作，再显式唤醒阻塞线程；退出不依赖游戏时间是否冻结。 */
    dominoThreadQueueStop(&g_domino_from_main_loop_queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueStop(&g_domino_to_main_loop_queue, DOMINO_THREAD_QUEUE_BOTH);
    dominoThreadQueueWakeAll(&g_domino_from_main_loop_queue);
    dominoThreadQueueWakeAll(&g_domino_to_main_loop_queue);

    int thread_result = CODE_OK;
    if (thrd_join(g_domino_main_loop_thread, &thread_result) != thrd_success) {
        return ERR_SYSTEM;
    }
    g_domino_main_loop_thread_started = false;
    return (DOMINO_CODE)thread_result;
}

DOMINO_CODE dominoEnginePushMsg(DominoEngineMessage* message_ptr, bool blocking) {
    return dominoThreadQueueProduce(&g_domino_to_main_loop_queue, message_ptr, blocking);
}

DOMINO_CODE dominoEnginePopMsg(DominoEngineMessage* message_out_ptr, bool blocking) {
    return dominoThreadQueueConsume(&g_domino_from_main_loop_queue, message_out_ptr, blocking);
}

DOMINO_CODE dominoLoopProduceToMain(DominoEngineMessage* message_ptr, bool blocking) {
    return dominoThreadQueueProduce(&g_domino_from_main_loop_queue, message_ptr, blocking);
}

DOMINO_CODE dominoEngineInit(DominoEngineLaunchConfig launch_config) {
    if (g_domino_engine_lifecycle_state != DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return ERR_ALREADY_INITIALIZED;
    }
    if (!launch_config.storage_path || launch_config.storage_path[0] == '\0') {
        return ERR_INVALID_PARAM;
    }
    size_t storage_path_len = strlen(launch_config.storage_path);
    if (storage_path_len >= sizeof(g_domino_storage_path)) {
        return ERR_OUT_OF_RANGE;
    }

    g_domino_engine_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_INITIALIZING;
    memcpy(g_domino_storage_path, launch_config.storage_path, storage_path_len + 1U);
    g_domino_engine_launch_config = launch_config;
    g_domino_engine_launch_config.storage_path = g_domino_storage_path;
    memset(g_domino_storage_account_id, 0, sizeof(g_domino_storage_account_id));
    (void)snprintf(g_domino_storage_run_mode, sizeof(g_domino_storage_run_mode), "%s", "local");

    DominoEngineInitStage stage = DOMINO_ENGINE_INIT_STAGE_NONE;
    DOMINO_CODE result = dominoLoggerModuleInit();
    if (result != CODE_OK) {
        goto fail;
    }
    stage = DOMINO_ENGINE_INIT_STAGE_LOGGER;
    dominoCommonModuleInit();
    stage = DOMINO_ENGINE_INIT_STAGE_COMMON;

    dominoHostModuleInit();
    stage = DOMINO_ENGINE_INIT_STAGE_HOST;
    result = dominoNavModuleInitBefore();
    if (result != CODE_OK) {
        goto fail;
    }
    stage = DOMINO_ENGINE_INIT_STAGE_NAV;

    dominoSocialModuleInit();
    stage = DOMINO_ENGINE_INIT_STAGE_SOCIAL;
    dominoHumanModuleInit();
    stage = DOMINO_ENGINE_INIT_STAGE_HUMAN;

    dominoTimeModuleInitBefore();

    result = dominoStorageModuleInit();
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "loadStorage failed (rc=%d)", result);
        goto fail;
    }

    result = dominoNavModuleInitAfter();
    if (result != CODE_OK) {
        goto fail;
    }

    dominoTimeModuleInitAfter();

    g_domino_client_thread = thrd_current();
    g_domino_engine_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_INITIALIZED;
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_INFO, "dominoEngineInit complete");

    return CODE_OK;

fail:
    dominoEngineRollbackInit(stage);
    return result;
}

static int dominoMainLoopThread(void* arg_ptr) {
    (void)arg_ptr;

    atomic_store(&g_domino_human_behavior_planning_stop, false);

    if (thrd_create(&g_domino_human_behavior_planning_thread, dominoHumanBehaviorPlanningThread, nullptr) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "failed to create human behavior planning thread");
        atomic_store(&g_domino_human_behavior_planning_stop, true);
        return ERR_THREAD_CREATE;
    }

    DominoEngineMessage message;
    while (true) {
        if (dominoThreadQueueIsStopped(&g_domino_to_main_loop_queue, DOMINO_THREAD_QUEUE_CONSUMER)) {
            break;
        }
        if (dominoTimeModuleIsFrozen()) {
            struct timespec sleep_time = {
                .tv_sec = 0,
                .tv_nsec = 1000000L,
            };
            (void)thrd_sleep(&sleep_time, nullptr);
            continue;
        }

        DOMINO_CODE code = dominoThreadQueueConsume(&g_domino_to_main_loop_queue, &message, true);
        if (code == ERR_QUEUE_PRODUCER_STOPPED || code == ERR_QUEUE_CONSUMER_STOPPED) {
            break;
        }
        if (code != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "failed to consume message from main loop queue (code=%d)",
                              code);
            continue;
        }

        code = dominoDispatchMessage(&message);
        if (code != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "failed to dispatch main loop message (type=%u, code=%d)",
                              message.type, code);
        }
    }

    atomic_store(&g_domino_human_behavior_planning_stop, true);
    if (thrd_join(g_domino_human_behavior_planning_thread, nullptr) != thrd_success) {
        return ERR_SYSTEM;
    }

    return CODE_OK;
}

DOMINO_CODE dominoEngineRun(DominoEngineClientData* client_data_out_ptr) {
    if (g_domino_engine_lifecycle_state == DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return ERR_NOT_INITIALIZED;
    }
    if (g_domino_engine_lifecycle_state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED) {
        return ERR_GAME_STATE_INVALID;
    }

    DOMINO_CODE result_code = dominoThreadQueueInit(&g_domino_to_main_loop_queue, sizeof(DominoEngineMessage), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                                    DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
    if (result_code != CODE_OK) {
        return result_code;
    }

    result_code = dominoThreadQueueInit(&g_domino_from_main_loop_queue, sizeof(DominoEngineMessage), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                        DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
    if (result_code != CODE_OK) {
        dominoThreadQueueDestroy(&g_domino_to_main_loop_queue);
        return result_code;
    }

    if (thrd_create(&g_domino_main_loop_thread, dominoMainLoopThread, nullptr) != thrd_success) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "failed to create main loop thread");
        dominoEngineQueueModuleExit();
        return ERR_THREAD_CREATE;
    }
    g_domino_main_loop_thread_started = true;

    if (client_data_out_ptr) {
        client_data_out_ptr->game_date_time_ns = dominoGetRuntimeDateTimeBase();
    }
    g_domino_engine_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_RUNNING;
    return CODE_OK;
}

DOMINO_CODE dominoEngineExit(void) {
    if (g_domino_engine_lifecycle_state == DOMINO_ENGINE_LIFECYCLE_UNINITIALIZED) {
        return ERR_NOT_INITIALIZED;
    }
    if (g_domino_engine_lifecycle_state != DOMINO_ENGINE_LIFECYCLE_INITIALIZED &&
        g_domino_engine_lifecycle_state != DOMINO_ENGINE_LIFECYCLE_RUNNING && g_domino_engine_lifecycle_state != DOMINO_ENGINE_LIFECYCLE_STOPPING) {
        return ERR_GAME_STATE_INVALID;
    }
    g_domino_engine_lifecycle_state = DOMINO_ENGINE_LIFECYCLE_STOPPING;

    DOMINO_CODE result = CODE_OK;
    if (g_domino_engine_exit_progress < DOMINO_ENGINE_EXIT_PROGRESS_THREADS_STOPPED) {
        result = dominoEngineShutdownMainLoopThread();
        if (result != CODE_OK) {
            return result;
        }
        dominoEngineQueueModuleExit();
        g_domino_engine_exit_progress = DOMINO_ENGINE_EXIT_PROGRESS_THREADS_STOPPED;
    }

    if (g_domino_engine_exit_progress < DOMINO_ENGINE_EXIT_PROGRESS_TIME_SETTLED) {
        dominoTimeModuleExit();
        g_domino_engine_exit_progress = DOMINO_ENGINE_EXIT_PROGRESS_TIME_SETTLED;
    }

    /* 保存失败保留内存世界；重试时仍由 Nav 的 dirty 标志决定是否重新压实。 */
    result = dominoNavModuleExitBefore();
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "prepare navigation for save failed (rc=%d)", result);
        return result;
    }

    const char* storage_path = g_domino_engine_launch_config.storage_path;
    if (storage_path && storage_path[0] != '\0') {
        result = dominoStorageModuleExit();
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_ERROR, "saveStorage failed (rc=%d); engine remains available for retry",
                              result);
            return result;
        }
    } else {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_INFO, "no storage path, skip saveStorage");
    }

    dominoHumanModuleExit();
    dominoSocialModuleExit();

    dominoNavModuleExitAfter();
    dominoHostModuleExit();

    dominoCommonModuleExit();
    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_INFO, "dominoEngineExit complete");
    dominoLoggerModuleExit();

    dominoEngineResetSessionState();
    return CODE_OK;
}
