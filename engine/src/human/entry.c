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
