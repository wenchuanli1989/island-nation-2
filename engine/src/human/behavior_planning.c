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

        // 仅输出底层纳秒诊断；后续任务调度和结算须使用 32 位现实毫秒入口。
        uint64_t now_ns = dominoTimeModuleGetDateTimeNow();
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "now_ns=%" PRIu64, now_ns);

        for (size_t i = 0; i < kv_size(domino_all_human_list); i++) {
            DominoHuman* human = &kv_A(domino_all_human_list, i);
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_HUMAN, DOMINO_LOG_LEVEL_DEBUG, "human id=%u thinking...", human->id);
        }
    }

    return 0;
}
