#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "../../../engine/src/game_timer/entry.h"
#include "../../../engine/src/human/executor.h"
#include "../../../engine/src/logger/entry.h"
#include "../../../engine/src/queue/thread_message.h"
#include "../../../engine/src/time/entry.h"
#include "domino_engine.h"
#include "domino_shared_common.h"

enum { TEST_WAITER_COUNT = 8U, TEST_ROUND_COUNT = 64U };

typedef struct TestWaiter {
    thrd_t thread;
    uint64_t epoch;
    _Atomic bool done_flag;
} TestWaiter;

typedef struct TestStopWatchdog {
    thrd_t thread;
    _Atomic uint64_t started_ns;
    _Atomic bool done_flag;
} TestStopWatchdog;

/** @brief 测试失败使用运行期检查，Release 下同样有效。 */
static void requireTest(bool condition, const char* message_str) {
    if (!condition) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "freeze test: %s", message_str);
    }
}

/** @brief 测试调度让步；实际业务时间不使用此等待时长。 */
static void pauseTest(void) {
    struct timespec remaining = {.tv_nsec = 1000000L};
    int result_code;
    do {
        result_code = thrd_sleep(&remaining, &remaining);
    } while (result_code == -1);
    requireTest(result_code == 0, "test scheduling sleep failed");
}

static void checkDeadline(uint64_t start_ns) {
    if (dominoMonotonicTimeNs() - start_ns > UINT64_C(5000000000)) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "freeze test: waiter did not make progress within 5 seconds");
    }
}

static void waitForCount(uint32_t expected_count) {
    const uint64_t start_ns = dominoMonotonicTimeNs();
    while (dominoTimeModuleGetFreezeWaiterCount() != expected_count) {
        checkDeadline(start_ns);
        pauseTest();
    }
}

static int waitOnce(void* arg_ptr) {
    TestWaiter* waiter_ptr = arg_ptr;
    dominoTimeModuleWait(waiter_ptr->epoch);
    atomic_store(&waiter_ptr->done_flag, true);
    return 0;
}

static void startWaiter(TestWaiter* waiter_ptr, uint64_t epoch) {
    waiter_ptr->epoch = epoch;
    atomic_store(&waiter_ptr->done_flag, false);
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&waiter_ptr->thread, waitOnce, waiter_ptr), ERR_THREAD_CREATE, "freeze test: thrd_create");
}

static void joinWaiter(TestWaiter* waiter_ptr) {
    const uint64_t start_ns = dominoMonotonicTimeNs();
    while (!atomic_load(&waiter_ptr->done_flag)) {
        checkDeadline(start_ns);
        pauseTest();
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(waiter_ptr->thread, nullptr), ERR_THREAD_JOIN, "freeze test: thrd_join");
}

/** @brief 独立验证冻结通知时序；引擎尚未初始化，没有其他 time 等待者。 */
static void testTimeWait(void) {
    dominoSetRuntimeDateTimeBase(UINT64_C(100000000));
    dominoTimeModuleInit();
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    requireTest(now_ms == 100U, "time Init must preserve the loaded clock base");
    requireTest(dominoTimeModuleGetFreezeEpoch() == 1U, "initial frozen epoch must be 1");
    requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "initial waiter count must be zero");

    TestWaiter waiter = {0};
    uint64_t epoch = dominoTimeModuleGetFreezeEpoch();
    startWaiter(&waiter, epoch);
    waitForCount(1U);
    dominoTimeModuleFreeze();
    requireTest(dominoTimeModuleGetFreezeEpoch() == epoch, "repeated Freeze must preserve the epoch");
    requireTest(dominoTimeModuleGetFreezeWaiterCount() == 1U, "repeated Freeze must preserve registered waiters");
    dominoTimeModuleWakeAll();
    pauseTest();
    requireTest(!atomic_load(&waiter.done_flag), "a notification alone must not end a frozen wait");
    requireTest(dominoTimeModuleGetFreezeWaiterCount() == 1U, "a notification alone must preserve the wait count");
    dominoTimeModuleThaw();
    joinWaiter(&waiter);
    requireTest(dominoTimeModuleGetFreezeEpoch() == 0U, "running clock must expose epoch zero");
    requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "returned wait must unregister itself");

    // 通知早于 Wait：仍持有旧冻结快照的线程不能错过解冻后睡下。
    startWaiter(&waiter, epoch);
    joinWaiter(&waiter);
    dominoTimeModuleFreeze();
    const uint64_t next_epoch = dominoTimeModuleGetFreezeEpoch();
    requireTest(next_epoch == epoch + 1U, "a new freeze must advance the epoch");
    startWaiter(&waiter, epoch);
    joinWaiter(&waiter);
    requireTest(dominoTimeModuleIsFrozen(), "old-epoch Wait must return even when a newer freeze is active");
    // 低 32 位与当前轮次相同的无效代次也必须返回，不能截断后误入等待。
    startWaiter(&waiter, next_epoch + (UINT64_C(1) << 32));
    joinWaiter(&waiter);
    startWaiter(&waiter, 0U);
    joinWaiter(&waiter);
    requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "nonwaiting calls must not change the count");

    for (uint32_t round_index = 0U; round_index < TEST_ROUND_COUNT; round_index++) {
        TestWaiter old_waiters[TEST_WAITER_COUNT] = {0};
        TestWaiter new_waiters[TEST_WAITER_COUNT] = {0};
        epoch = dominoTimeModuleGetFreezeEpoch();
        for (uint32_t index = 0U; index < TEST_WAITER_COUNT; index++) {
            startWaiter(&old_waiters[index], epoch);
        }
        waitForCount(TEST_WAITER_COUNT);
        // 不等待旧线程返回，立即开启下一轮，并让新旧等待者竞争同一把锁。
        dominoTimeModuleThaw();
        dominoTimeModuleFreeze();
        const uint64_t current_epoch = dominoTimeModuleGetFreezeEpoch();
        requireTest(current_epoch == epoch + 1U, "rapid refreeze must advance the epoch");
        for (uint32_t index = 0U; index < TEST_WAITER_COUNT; index++) {
            startWaiter(&new_waiters[index], current_epoch);
        }
        for (uint32_t index = 0U; index < TEST_WAITER_COUNT; index++) {
            joinWaiter(&old_waiters[index]);
        }
        waitForCount(TEST_WAITER_COUNT);
        requireTest(dominoTimeModuleIsFrozen(), "old waiters returning must not thaw the new generation");
        dominoTimeModuleThaw();
        for (uint32_t index = 0U; index < TEST_WAITER_COUNT; index++) {
            joinWaiter(&new_waiters[index]);
        }
        requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "old returns must not corrupt the new generation's count");
        dominoTimeModuleFreeze();
    }
    dominoTimeModuleDestroy();
    dominoTimeModuleInit();
    dominoTimeGetRuntimeMs(&now_ms);
    requireTest(now_ms == 0U, "Destroy must clear the in-memory clock base");
    requireTest(dominoTimeModuleGetFreezeEpoch() == 1U, "Destroy must restore the initial frozen epoch");
    requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "Destroy must clear waiter accounting");
    epoch = dominoTimeModuleGetFreezeEpoch();
    startWaiter(&waiter, epoch);
    waitForCount(1U);
    dominoTimeModuleThaw();
    joinWaiter(&waiter);
    dominoTimeModuleFreeze();
    dominoTimeModuleDestroy();
    DOMINO_PRINT_MESSAGE("freeze time tests passed (%u rapid refreeze rounds)", TEST_ROUND_COUNT);
}

static void initEngine(const char* storage_path_str) {
    dominoEngineInit((DominoEngineLaunchConfig){.storage_path = storage_path_str});
    requireTest(dominoTimeModuleGetFreezeEpoch() == 1U, "engine Init must start with the initial epoch");
    requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "engine Init must have no waiters");
}

/** @brief 唤醒原本空闲的队列消费者，确保测试实际覆盖三个业务线程的 time Wait。 */
static void enterFrozenWaits(void) {
    dominoEngineFreezeTime();
    DominoEngineMessage message = {.type = DOMINO_ENGINE_MSG_NONE};
    requireTest(dominoEnginePushMsg(&message) == CODE_OK, "cannot wake the frozen main loop");
    dominoHumanExecutionNotifyTimeChanged();
    waitForCount(3U);
}

/** @brief 冻结期间登记仍可用；恢复沿用原截止点，并由原 GameTimer 通知路径投递。 */
static void testFrozenTimer(DominoThreadQueue* events_ptr) {
    domino_runtime_ms_t frozen_ms;
    dominoTimeGetRuntimeMs(&frozen_ms);
    requireTest(frozen_ms <= UINT32_MAX - 10U, "test timer deadline exceeds the business clock range");
    const domino_runtime_ms_t due_ms = frozen_ms + 10U;
    domino_game_timer_id_t timer_id;
    requireTest(dominoGameTimerScheduleAt(events_ptr, due_ms, 7U, UINT64_C(123), &timer_id) == CODE_OK, "cannot register a timer while frozen");
    for (uint32_t index = 0U; index < 20U; index++) {
        pauseTest();
    }
    domino_runtime_ms_t now_ms;
    dominoTimeGetRuntimeMs(&now_ms);
    requireTest(now_ms == frozen_ms, "frozen time must ignore real elapsed time");
    DominoThreadMessage message;
    requireTest(dominoThreadQueueConsume(events_ptr, &message, false) == ERR_QUEUE_EMPTY, "future timer must not expire during freeze");
    dominoEngineThawTime();
    const uint64_t start_ns = dominoMonotonicTimeNs();
    DOMINO_CODE code;
    while ((code = dominoThreadQueueConsume(events_ptr, &message, false)) == ERR_QUEUE_EMPTY) {
        checkDeadline(start_ns);
        pauseTest();
    }
    requireTest(code == CODE_OK && message.type == DOMINO_THREAD_MESSAGE_GAME_TIMER, "restored timer must deliver its event");
    requireTest(message.payload.game_timer.game_timer_id == timer_id && message.payload.game_timer.due_ms == due_ms &&
                    message.payload.game_timer.data_id == UINT64_C(123) && message.payload.game_timer.event_type == 7U,
                "restored timer must preserve its ID, deadline, and payload");
    dominoTimeGetRuntimeMs(&now_ms);
    requireTest(now_ms >= due_ms, "timer delivery must not precede the original deadline");
}

static void testEngineLifecycle(const char* storage_path_str) {
    initEngine(storage_path_str);
    domino_runtime_ms_t loaded_ms;
    dominoTimeGetRuntimeMs(&loaded_ms);
    pauseTest();
    dominoEngineStop();
    domino_runtime_ms_t stopped_ms;
    dominoTimeGetRuntimeMs(&stopped_ms);
    requireTest(stopped_ms == loaded_ms, "Init-only Stop must not advance time");
    requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "Init-only Stop must have no waiters");
    dominoEngineDestroy();

    for (uint32_t round_index = 0U; round_index < 2U; round_index++) {
        initEngine(storage_path_str);
        dominoTimeGetRuntimeMs(&loaded_ms);
        requireTest(loaded_ms == stopped_ms, "rebuilding must restore the saved clock base");
        dominoEngineStart(nullptr);
        pauseTest();
        DominoThreadQueue timer_events = {0};
        if (round_index == 1U) {
            dominoThreadQueueInit(&timer_events, sizeof(DominoThreadMessage), 2U, 2U);
            enterFrozenWaits();
            testFrozenTimer(&timer_events);
            waitForCount(0U);
            enterFrozenWaits();
            const uint64_t epoch = dominoTimeModuleGetFreezeEpoch();
            dominoEngineFreezeTime();
            requireTest(dominoTimeModuleGetFreezeEpoch() == epoch, "repeated engine Freeze must preserve the epoch");
            requireTest(dominoTimeModuleGetFreezeWaiterCount() == 3U, "repeated engine Freeze must preserve waiters");
            dominoTimeGetRuntimeMs(&loaded_ms);
        }
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_CORE, DOMINO_LOG_LEVEL_INFO, "FREEZE_LOG_MARKER_%" PRIu32, round_index);
        dominoEngineStop();
        DOMINO_PRINT_MESSAGE("STOP_RETURNED_%" PRIu32, round_index);
        dominoTimeGetRuntimeMs(&stopped_ms);
        requireTest(dominoTimeModuleIsFrozen(), "Stop must leave the clock frozen");
        requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "Stop must reclaim all time waiters");
        if (round_index == 1U) {
            requireTest(stopped_ms == loaded_ms, "frozen Stop must not resume the clock");
        }
        pauseTest();
        domino_runtime_ms_t after_stop_ms;
        dominoTimeGetRuntimeMs(&after_stop_ms);
        requireTest(after_stop_ms == stopped_ms, "time must remain fixed after Stop returns");
        if (round_index == 1U) {
            dominoThreadQueueDestroy(&timer_events);
        }
        dominoEngineDestroy();
    }
    initEngine(storage_path_str);
    dominoTimeGetRuntimeMs(&loaded_ms);
    requireTest(loaded_ms == stopped_ms, "final rebuild must preserve accumulated runtime");
    dominoEngineStop();
    dominoEngineDestroy();
    DOMINO_PRINT_MESSAGE("freeze engine lifecycle tests passed");
}

/** @brief Stop 同步阻塞时仍能报告超时；不访问引擎状态或参与其唤醒。 */
static int watchStopProgress(void* arg_ptr) {
    TestStopWatchdog* watchdog_ptr = arg_ptr;
    while (!atomic_load(&watchdog_ptr->done_flag)) {
        const uint64_t started_ns = atomic_load(&watchdog_ptr->started_ns);
        if (started_ns != 0U && dominoMonotonicTimeNs() - started_ns > UINT64_C(5000000000) && atomic_load(&watchdog_ptr->started_ns) == started_ns) {
            DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "freeze test: Stop race did not complete within 5 seconds");
        }
        pauseTest();
    }
    return 0;
}

/** @brief 覆盖线程尚未入睡及旧轮返回重入期间的冻结退出，每轮保留真实销毁和存档重建。 */
static void testStopRaces(const char* storage_path_str) {
    TestStopWatchdog watchdog = {0};
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&watchdog.thread, watchStopProgress, &watchdog), ERR_THREAD_CREATE,
                                  "freeze stop race: thrd_create watchdog");
    domino_runtime_ms_t previous_stopped_ms = 0U;
    for (uint32_t round_index = 0U; round_index < 2U * TEST_ROUND_COUNT; round_index++) {
        initEngine(storage_path_str);
        domino_runtime_ms_t loaded_ms;
        dominoTimeGetRuntimeMs(&loaded_ms);
        if (round_index != 0U) {
            requireTest(loaded_ms == previous_stopped_ms, "Stop race rebuild must preserve the saved time");
        }
        dominoEngineStart(nullptr);
        if (round_index >= TEST_ROUND_COUNT) {
            // 只在初始布置时等到三个冻结等待者；解冻后的新一轮不等待其返回或重新登记。
            enterFrozenWaits();
            atomic_store(&watchdog.started_ns, dominoMonotonicTimeNs());
            dominoEngineThawTime();
        } else {
            // 不等业务线程空闲或登记冻结等待，使 Stop 与它们首次观察冻结竞争。
            atomic_store(&watchdog.started_ns, dominoMonotonicTimeNs());
        }
        dominoEngineFreezeTime();
        domino_runtime_ms_t frozen_ms;
        dominoTimeGetRuntimeMs(&frozen_ms);
        dominoEngineStop();
        dominoTimeGetRuntimeMs(&previous_stopped_ms);
        requireTest(previous_stopped_ms == frozen_ms, "Stop race must keep the frozen settlement time");
        requireTest(dominoTimeModuleIsFrozen(), "Stop race must leave the clock frozen");
        requireTest(dominoTimeModuleGetFreezeWaiterCount() == 0U, "Stop race must reclaim all time waiters");
        pauseTest();
        domino_runtime_ms_t after_stop_ms;
        dominoTimeGetRuntimeMs(&after_stop_ms);
        requireTest(after_stop_ms == frozen_ms, "Stop race must not restart time after returning");
        atomic_store(&watchdog.started_ns, 0U);
        dominoEngineDestroy();
    }
    initEngine(storage_path_str);
    domino_runtime_ms_t loaded_ms;
    dominoTimeGetRuntimeMs(&loaded_ms);
    requireTest(loaded_ms == previous_stopped_ms, "last Stop race rebuild must preserve the saved time");
    dominoEngineStop();
    dominoEngineDestroy();
    atomic_store(&watchdog.done_flag, true);
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(watchdog.thread, nullptr), ERR_THREAD_JOIN, "freeze stop race: thrd_join watchdog");
    DOMINO_PRINT_MESSAGE("freeze Stop race tests passed (%u immediate Freeze/Stop, %u immediate Thaw/Freeze/Stop)", TEST_ROUND_COUNT,
                         TEST_ROUND_COUNT);
}

static int stopFromWrongThread(void* arg_ptr) {
    (void)arg_ptr;
    dominoEngineStop();
    return 0;
}

/** @brief 非法入口由 runner 在独立进程执行，必须直接失败退出。 */
static void testInvalidLifecycle(const char* case_str, const char* storage_path_str) {
    if (strcmp(case_str, "start-before-init") == 0) {
        dominoEngineStart(nullptr);
    } else {
        initEngine(storage_path_str);
        if (strcmp(case_str, "freeze-before-start") == 0) {
            dominoEngineFreezeTime();
        } else if (strcmp(case_str, "destroy-before-stop") == 0) {
            dominoEngineDestroy();
        } else if (strcmp(case_str, "wrong-thread-stop") == 0) {
            thrd_t thread;
            DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&thread, stopFromWrongThread, nullptr), ERR_THREAD_CREATE, "freeze test: thrd_create");
            DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(thread, nullptr), ERR_THREAD_JOIN, "freeze test: thrd_join");
        } else if (strcmp(case_str, "duplicate-stop") == 0) {
            dominoEngineStop();
            dominoEngineStop();
        } else if (strcmp(case_str, "thaw-after-stop") == 0) {
            dominoEngineStop();
            dominoEngineThawTime();
        } else if (strcmp(case_str, "thaw-while-running") == 0) {
            dominoEngineStart(nullptr);
            dominoEngineThawTime();
        } else {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "unknown freeze test case: %s", case_str);
        }
    }
    // 成功返回 0 可让 runner 区分“接口错误地接受了调用”和预期的致命拒绝。
}

int main(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "time") == 0) {
        testTimeWait();
    } else if (argc == 3 && strcmp(argv[1], "lifecycle") == 0) {
        testEngineLifecycle(argv[2]);
    } else if (argc == 3 && strcmp(argv[1], "stop-races") == 0) {
        testStopRaces(argv[2]);
    } else if (argc == 4 && strcmp(argv[1], "invalid") == 0) {
        testInvalidLifecycle(argv[2], argv[3]);
    } else {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "usage: %s time | lifecycle STORAGE_PATH | stop-races STORAGE_PATH | invalid CASE STORAGE_PATH",
                               argv[0]);
    }
    return 0;
}
