#ifndef DOMINO_ENGINE_HUMAN_MODULE_H
#define DOMINO_ENGINE_HUMAN_MODULE_H

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @brief 初始化人物容器、任务存储、执行／规划队列及控制资源，不创建线程。
 * @pre 引擎串行管理调用顺序；首次使用静态空状态，再次使用前须完成 Destroy。
 */
extern void dominoHumanModuleInit(void);

/** @brief 在冻结世界下依次启动执行和规划线程并等待就绪，不重新分配基础资源。
 * @pre 引擎已完成 Init，处于 STARTING；本次实例尚未启动。
 */
extern void dominoHumanModuleStart(void);

/** @brief 依次 join 执行线程和规划线程，保留所有人物及运行资源。
 * @pre 线程已创建，引擎已进入 STOPPING 并唤醒业务线程；timer 和投递目标仍有效。
 * @note 仅 Init、未 Start 的实例由引擎跳过本接口；内部不重复校验生命周期。
 */
extern void dominoHumanModuleStop(void);

/** @brief 释放规划／执行资源、任务存储及人物容器，不停止或 join 线程。
 * @pre 引擎已完成 Stop 及保存；运行过的实例已 join 全部业务线程并在保存前完成统一结算。
 * @note 支持仅 Init 的资源释放；人物容器须保留到运行资源释放完成，顺序由引擎保证。
 */
extern void dominoHumanModuleDestroy(void);

extern DominoHuman* dominoGetHumanByID(domino_human_id_t human_id);

/** @brief Human 与 Data 共用 human_id 索引；加载期间可在 data 指针绑定前查询。 */
extern DominoHumanData* dominoGetHumanDataByID(domino_human_id_t human_id);

KHASHL_MAP_INIT(KH_LOCAL, DominoHumanIDMap, dominoHumanIdMap, domino_human_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(DominoHuman) DominoHumanVec;
typedef kvec_t(DominoHumanData) DominoHumanDataVec;

/** @brief 两个数组按相同索引一一配对，由 human_id 共用同一张 ID 映射。 */
extern DominoHumanVec domino_all_human_list;
extern DominoHumanDataVec domino_all_human_data_list;
extern DominoHumanIDMap* dominoHumanIdMap;

#endif
