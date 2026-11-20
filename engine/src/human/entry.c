#include "entry.h"

#include "advance.h"
#include "domino_shared_common.h"
#include "executor.h"
#include "rules.h"
#include "state.h"
#include "store.h"

DominoHumanVec domino_all_human_list;
DominoHumanDataVec domino_all_human_data_list;
DominoHumanIDMap* dominoHumanIdMap;
DominoRWLock domino_human_data_lock;

/* 仅记录本线程的读取范围，不参与串行加载与保存。 */
static thread_local bool g_read_lock_held;

void dominoHumanReadLock(void) {
    if (g_read_lock_held) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "human read lock is not recursive");
    }
    dominoRWLockReadLock(&domino_human_data_lock);
    g_read_lock_held = true;
}

void dominoHumanReadUnlock(void) {
    if (!g_read_lock_held) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "human read unlock requires a held read lock");
    }
    g_read_lock_held = false;
    dominoRWLockReadUnlock(&domino_human_data_lock);
}

DominoHuman* dominoGetHumanByID(domino_human_id_t human_id) {
    const khint_t key = dominoHumanIdMap_get(dominoHumanIdMap, human_id);
    if (kh_exist(dominoHumanIdMap, key)) {
        return &kv_A(domino_all_human_list, kh_val(dominoHumanIdMap, key));
    }
    return nullptr;
}

#ifdef DOMINO_PLAYGROUND_MODE
/* 测试验收专用：人物输入入口，每个入口投递独立的场景消息；清理测试代码时一并删除。 */

void dominoHumanSubmitSleepConditionForTest(domino_human_id_t human_id, bool forbidden_flag) {
    DominoHumanExecutionMessage message = {
        .type = DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_SLEEP_CONDITION,
        .payload.test_sleep_condition = {.human_id = human_id, .forbidden_flag = forbidden_flag},
    };
    dominoHumanExecutionSubmit(&message);
}

DOMINO_CODE dominoHumanSubmitEnergyForTest(domino_human_id_t human_id, uint8_t energy) {
    if (energy > DOMINO_HUMAN_NEED_MAX) {
        return ERR_INVALID_PARAM;
    }
    DominoHumanExecutionMessage message = {
        .type = DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_ENERGY,
        .payload.test_energy = {.human_id = human_id, .energy = energy},
    };
    dominoHumanExecutionSubmit(&message);
    return CODE_OK;
}

DOMINO_CODE dominoHumanSubmitStimulusForTest(domino_human_id_t human_id, uint8_t stimulus_kind, uint32_t source_id) {
    if (stimulus_kind >= DOMINO_HUMAN_STIMULUS_COUNT) {
        return ERR_INVALID_PARAM;
    }
    DominoHumanExecutionMessage message = {
        .type = DOMINO_HUMAN_EXECUTION_MESSAGE_TEST_STIMULUS,
        .payload.test_stimulus = {.human_id = human_id, .source_id = source_id, .stimulus_kind = stimulus_kind},
    };
    dominoHumanExecutionSubmit(&message);
    return CODE_OK;
}
#endif

/** @brief 正常退出时释放已初始化的人物容器。 */
static void releaseHumanEntities(void) {
    dominoHumanIdMap_destroy(dominoHumanIdMap);
    dominoHumanIdMap = nullptr;
    kv_destroy(domino_all_human_list);
    kv_destroy(domino_all_human_data_list);
    kv_init(domino_all_human_list);
    kv_init(domino_all_human_data_list);
}

void dominoHumanModuleInit(void) {
    dominoHumanAiConfigInit();
    dominoRWLockInit(&domino_human_data_lock);
    dominoHumanIdMap = dominoHumanIdMap_init();
    if (!dominoHumanIdMap) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human ID map");
    }
    dominoHumanStoreInit();
    dominoHumanExecutionInit();
}

void dominoHumanModuleInitAfter(void) {
    dominoHumanExecutionInitAfter();
}

void dominoHumanModuleStart(void) {
    dominoHumanExecutionStart();
}

void dominoHumanModuleNotifyTimeChanged(void) {
    dominoHumanExecutionNotifyTimeChanged();
}

void dominoHumanModuleStop(void) {
    dominoHumanExecutionStop();
}

void dominoHumanModuleSettle(domino_runtime_ms_t now_ms) {
    dominoHumanSettleAll(now_ms);
}

void dominoHumanModuleDestroy(void) {
    dominoHumanExecutionDestroy();
    dominoHumanStoreDestroy();
    releaseHumanEntities();
    dominoRWLockDestroy(&domino_human_data_lock);
    dominoHumanAiConfigDestroy();
}
