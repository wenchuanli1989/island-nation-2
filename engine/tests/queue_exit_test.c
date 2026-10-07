#define _POSIX_C_SOURCE 200809L

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "domino_engine.h"
#include "domino_shared_common.h"
#include "entry.h"

/** @brief 验证空队列、冻结及刚启动时的主循环均可正常退出。 */
static bool runExitScenario(const char* scenario_name, bool freeze_flag, bool wait_for_loop_flag) {
    bool success_flag = false;
    bool engine_active_flag = false;
    char temp_root[] = "/tmp/domino-queue-exit.XXXXXX";
    if (!mkdtemp(temp_root)) {
        (void)fprintf(stderr, "%s: mkdtemp failed\n", scenario_name);
        return false;
    }

    char world_path[128];
    (void)snprintf(world_path, sizeof(world_path), "%s/world", temp_root);
    DominoEngineLaunchConfig config = {.storage_path = world_path, .storage_integrity_verify = false};
    DOMINO_CODE result_code = dominoEngineInit(config);
    if (result_code != CODE_OK) {
        (void)fprintf(stderr, "%s: initialization failed (%d)\n", scenario_name, result_code);
        goto cleanup;
    }
    engine_active_flag = true;

    if (freeze_flag && (result_code = dominoEngineFreezeTime()) != CODE_OK) {
        (void)fprintf(stderr, "%s: freeze failed (%d)\n", scenario_name, result_code);
        goto cleanup;
    }
    result_code = dominoEngineRun(nullptr);
    if (result_code != CODE_OK) {
        (void)fprintf(stderr, "%s: run failed (%d)\n", scenario_name, result_code);
        goto cleanup;
    }

    if (wait_for_loop_flag) {
        const struct timespec poll_time = {.tv_sec = 0, .tv_nsec = 1000000L};
        while (atomic_load(&g_domino_human_behavior_planning_stop)) {
            (void)thrd_sleep(&poll_time, nullptr);
        }
        /* 给空队列等待或冻结循环运行的机会；CTest 超时负责检测退出死锁。 */
        const struct timespec settle_time = {.tv_sec = 0, .tv_nsec = 10000000L};
        (void)thrd_sleep(&settle_time, nullptr);
    }

    result_code = dominoEngineExit();
    if (result_code != CODE_OK) {
        (void)fprintf(stderr, "%s: exit failed (%d)\n", scenario_name, result_code);
        goto cleanup;
    }
    engine_active_flag = false;
    success_flag = true;

cleanup:
    if (engine_active_flag) {
        (void)dominoEngineExit();
    }
    if (dominoRemovePathRecursive(temp_root) != 0) {
        (void)fprintf(stderr, "%s: temporary directory cleanup failed\n", scenario_name);
        success_flag = false;
    }
    return success_flag;
}

int main(void) {
    if (!runExitScenario("empty queues", false, true) || !runExitScenario("frozen loop", true, true) ||
        !runExitScenario("immediate exit", false, false)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
