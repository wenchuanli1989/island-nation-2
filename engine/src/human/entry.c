#include "entry.h"

#include "domino_shared_common.h"
#include "executor.h"
#include "state.h"
#include "store.h"

DominoHumanVec domino_all_human_list;
DominoHumanDataVec domino_all_human_data_list;
DominoHumanIDMap* dominoHumanIdMap;
DominoRWLock domino_human_data_lock;

/* 仅记录本线程的读取范围，不复制引擎生命周期，也不参与串行加载与保存。 */
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
    khint_t key = dominoHumanIdMap_get(dominoHumanIdMap, human_id);
    if (kh_exist(dominoHumanIdMap, key)) {
        return &kv_A(domino_all_human_list, kh_val(dominoHumanIdMap, key));
    }
    return nullptr;
}

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
    dominoRWLockInit(&domino_human_data_lock);
    dominoHumanIdMap = dominoHumanIdMap_init();
    if (!dominoHumanIdMap) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human ID map");
    }

    dominoHumanStoreInit();
    dominoHumanExecutionInit();
}

void dominoHumanModuleStart(void) {
    /* time 仍冻结；引擎在全部线程创建后解冻，无需没有准备工作的启动握手。 */
    dominoHumanExecutionStart();
}

void dominoHumanModuleStop(void) {
    dominoHumanExecutionStop();
}

void dominoHumanModuleDestroy(void) {
    dominoHumanExecutionDestroy();
    dominoHumanStoreDestroy();
    releaseHumanEntities();
    dominoRWLockDestroy(&domino_human_data_lock);
}
