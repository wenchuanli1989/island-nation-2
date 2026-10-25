#include "entry.h"

#include "domino_shared_common.h"
#include "executor.h"
#include "planner.h"
#include "store.h"

DominoHumanVec domino_all_human_list;
DominoHumanDataVec domino_all_human_data_list;
DominoHumanIDMap* dominoHumanIdMap;

DominoHuman* dominoGetHumanByID(domino_human_id_t human_id) {
    khint_t key = dominoHumanIdMap_get(dominoHumanIdMap, human_id);
    if (kh_exist(dominoHumanIdMap, key)) {
        return &kv_A(domino_all_human_list, kh_val(dominoHumanIdMap, key));
    }
    return nullptr;
}

DominoHumanData* dominoGetHumanDataByID(domino_human_id_t human_id) {
    khint_t key = dominoHumanIdMap_get(dominoHumanIdMap, human_id);
    if (!kh_exist(dominoHumanIdMap, key)) {
        return nullptr;
    }
    return &kv_A(domino_all_human_data_list, kh_val(dominoHumanIdMap, key));
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
    dominoHumanIdMap = dominoHumanIdMap_init();
    if (!dominoHumanIdMap) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "cannot allocate human ID map");
    }

    dominoHumanStoreInit();
    dominoHumanExecutionInit();
}

void dominoHumanModuleStart(void) {
    /* time 仍冻结；执行器就绪后再启动规划线程，全部就绪后由引擎解冻。 */
    dominoHumanExecutionStart();
    dominoHumanPlannerStart();
}

void dominoHumanModuleStop(void) {
    /* 执行器最后一批仍可提交规划；先结束生产者，再 join 规划线程。 */
    dominoHumanExecutionStop();
    dominoHumanPlannerStop();
}

void dominoHumanModuleDestroy(void) {
    dominoHumanPlannerDestroy();
    dominoHumanExecutionDestroy();
    dominoHumanStoreDestroy();
    releaseHumanEntities();
}
