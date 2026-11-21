#include "entry.h"

#include "advance.h"
#include "domino_shared_common.h"
#include "executor.h"
#include "rules.h"
#include "state.h"
#include "store.h"

DominoHumanVec g_domino_human_list;
DominoHumanDataVec g_domino_human_data_list;
DominoHumanIdMap* g_domino_human_id_map;
DominoRWLock g_domino_human_lock;

// 只用于检测本线程的递归读锁。
static thread_local bool g_read_lock_held_flag;

void dominoHumanReadLock(void) {
    if (g_read_lock_held_flag) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "human read lock is not recursive");
    }
    dominoRWLockReadLock(&g_domino_human_lock);
    g_read_lock_held_flag = true;
}

void dominoHumanReadUnlock(void) {
    if (!g_read_lock_held_flag) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "human read unlock requires a held read lock");
    }
    g_read_lock_held_flag = false;
    dominoRWLockReadUnlock(&g_domino_human_lock);
}

DominoHuman* dominoGetHumanById(domino_human_id_t human_id) {
    const khint_t key = dominoHumanIdMap_get(g_domino_human_id_map, human_id);
    if (kh_exist(g_domino_human_id_map, key)) {
        return &kv_A(g_domino_human_list, kh_val(g_domino_human_id_map, key));
    }
    return nullptr;
}

#ifdef DOMINO_PLAYGROUND_MODE
/* 测试验收专用：人物输入入口；清理测试代码时一并删除。 */

void dominoHumanSubmitSleepConditionForTest(domino_human_id_t human_id, bool forbidden_flag) {
    DominoHumanInboxMessage message = {
        .type = DOMINO_HUMAN_INBOX_MESSAGE_TEST_SLEEP_CONDITION,
        .payload.test_sleep_condition = {.human_id = human_id, .forbidden_flag = forbidden_flag},
    };
    dominoHumanExecutorSubmit(&message);
}

DOMINO_CODE dominoHumanSubmitEnergyForTest(domino_human_id_t human_id, uint8_t energy) {
    if (energy > DOMINO_HUMAN_NEED_MAX) {
        return ERR_INVALID_PARAM;
    }
    DominoHumanInboxMessage message = {
        .type = DOMINO_HUMAN_INBOX_MESSAGE_TEST_ENERGY,
        .payload.test_energy = {.human_id = human_id, .energy = energy},
    };
    dominoHumanExecutorSubmit(&message);
    return CODE_OK;
}

DOMINO_CODE dominoHumanSubmitStimulusForTest(domino_human_id_t human_id, uint8_t stimulus_kind, uint32_t source_id) {
    if (stimulus_kind >= DOMINO_HUMAN_STIMULUS_COUNT) {
        return ERR_INVALID_PARAM;
    }
    DominoHumanInboxMessage message = {
        .type = DOMINO_HUMAN_INBOX_MESSAGE_TEST_STIMULUS,
        .payload.test_stimulus = {.human_id = human_id, .source_id = source_id, .stimulus_kind = stimulus_kind},
    };
    dominoHumanExecutorSubmit(&message);
    return CODE_OK;
}
#endif

/** @brief 释放人物容器和映射表并复位为空。 */
static void releaseHumanEntities(void) {
    dominoHumanIdMap_destroy(g_domino_human_id_map);
    g_domino_human_id_map = nullptr;
    kv_destroy(g_domino_human_list);
    kv_destroy(g_domino_human_data_list);
    kv_init(g_domino_human_list);
    kv_init(g_domino_human_data_list);
}

void dominoHumanModuleInit(void) {
    dominoHumanAiConfigInit();
    dominoRWLockInit(&g_domino_human_lock);
    g_domino_human_id_map = dominoHumanIdMap_init();
    if (!g_domino_human_id_map) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human ID map");
    }
    dominoHumanStoreInit();
    dominoHumanExecutorInit();
}

void dominoHumanModuleInitAfter(void) {
    dominoHumanExecutorInitAfter();
}

void dominoHumanModuleStart(void) {
    dominoHumanExecutorStart();
}

void dominoHumanModuleNotifyTimeChanged(void) {
    dominoHumanExecutorNotifyTimeChanged();
}

void dominoHumanModuleStop(void) {
    dominoHumanExecutorStop();
}

void dominoHumanModuleSettle(domino_runtime_ms_t now_ms) {
    dominoHumanSettleAll(now_ms);
}

void dominoHumanModuleDestroy(void) {
    dominoHumanExecutorDestroy();
    dominoHumanStoreDestroy();
    releaseHumanEntities();
    dominoRWLockDestroy(&g_domino_human_lock);
    dominoHumanAiConfigDestroy();
}
