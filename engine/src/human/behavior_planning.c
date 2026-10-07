#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>
#include <time.h>

#include "../entry.h"
#include "../logger/entry.h"
#include "../time/entry.h"
#include "entry.h"

/** @brief 当前仅遍历人类并输出调试日志，尚未生成任务或行为。 */
int dominoHumanBehaviorPlanningThread(void* arg_ptr) {
    (void)arg_ptr;

    while (!atomic_load(&g_domino_human_behavior_planning_stop)) {
        if (dominoTimeModuleIsFrozen()) {
            struct timespec sleep_time = {
                .tv_sec = 0,
                .tv_nsec = 1000000L,
            };
            (void)thrd_sleep(&sleep_time, nullptr);
            continue;
        }

        uint64_t now = dominoTimeModuleGetDateTimeNow();
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "now=%" PRIu64, now);

        for (size_t i = 0; i < kv_size(domino_all_human_list); i++) {
            DominoHuman* human = &kv_A(domino_all_human_list, i);
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "human id=%u thinking...", human->id);
        }
    }

    return 0;
}
