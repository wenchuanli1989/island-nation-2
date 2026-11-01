#ifndef DOMINO_ENGINE_HUMAN_STATE_H
#define DOMINO_ENGINE_HUMAN_STATE_H

#include "domino_shared_rwlock.h"
#include "entry.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @file state.h
 * @brief human 内部可写存储；仅执行器调用链和串行加载、结算、保存、销毁使用。
 * @note 运行期由执行器在批次外层持写锁，内部不重复加锁。其他线程经 entry.h 取得读锁并直接查询原始对象，修改提交命令。
 * 一把锁同时保护 Human、Data、Task、Behavior、容器布局及 ID 索引；冻结和 STOPPING 末批仍遵循此约定。
 * 串行阶段直接访问，不取得锁。借用指针不能跨容器扩容、删除或重排保存。
 */
KHASHL_MAP_INIT(KH_LOCAL, DominoHumanIDMap, dominoHumanIdMap, domino_human_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

typedef kvec_t(DominoHuman) DominoHumanVec;
typedef kvec_t(DominoHumanData) DominoHumanDataVec;
typedef kvec_t(DominoTask) DominoTaskVec;
typedef kvec_t(DominoBehavior) DominoBehaviorVec;

extern DominoRWLock domino_human_data_lock;
extern DominoHumanVec domino_all_human_list;
extern DominoHumanDataVec domino_all_human_data_list;
extern DominoHumanIDMap* dominoHumanIdMap;
extern DominoTaskVec domino_all_task_list;
extern DominoBehaviorVec domino_all_behavior_list;

#endif
