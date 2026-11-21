#ifndef DOMINO_ENGINE_HUMAN_STORE_H
#define DOMINO_ENGINE_HUMAN_STORE_H

#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"

/** @file store.h
 * @brief 动机／任务／行为池：分配正式 ID、入池建索引、回收，并维护人物的动机清单、任务清单和待执行队列。
 * @note 容量和业务条件由调用方预检，本层只处理取号、存储和真实资源失败（致命退出）。正式 ID 递增分配、永不复用；
 * 回收采用交换删除，调用后不得保留池内借用指针。存储层可直接调用 Import* 做可信加载。
 */

/** @brief 创建三个池的 ID 索引。 */
void dominoHumanStoreInit(void);

/** @brief 释放全部任务独占计划块、三个池及索引，复位计数和计划版本。 */
void dominoHumanStoreDestroy(void);

/** @brief 按值导入可信动机并建立索引。
 * @return 动机池已满返回 ERR_CAPACITY_EXCEEDED。
 */
DOMINO_CODE dominoHumanStoreImportMotivation(const DominoMotivation* motivation_ptr);

/** @brief 按值导入可信任务及其计划项，计划块由本层复制并独占；task_ptr->plan_item_list 被忽略。
 * @return 任务池或计划项总量已满返回 ERR_CAPACITY_EXCEEDED。
 */
DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr, const DominoBehaviorPlanItem* plan_item_list, uint8_t plan_item_count);

/** @brief 按值导入可信行为记录并建立索引。
 * @return 行为池已满返回 ERR_CAPACITY_EXCEEDED。
 */
DOMINO_CODE dominoHumanStoreImportBehavior(const DominoBehavior* behavior_ptr);

/** @brief 当前各池数量及计划项总量，供容量预检。 */
uint32_t dominoHumanStoreMotivationCount(void);
uint32_t dominoHumanStoreTaskCount(void);
uint32_t dominoHumanStoreBehaviorCount(void);
uint32_t dominoHumanStorePlanItemCount(void);

/** @brief 取得本次 Init 内唯一的非零计划版本。 */
uint64_t dominoHumanStoreNextPlanVersion(void);

/** @brief 分配动机 ID 并入池，追加到人物动机清单；init_ptr 的身份字段被覆盖。
 * @return 新动机的池内地址，下次动机池变化前有效。
 */
DominoMotivation* dominoHumanStoreAddMotivation(DominoHumanData* data_ptr, const DominoMotivation* init_ptr);

/** @brief 分配 task_id 建立未启动的任务，追加到人物任务清单，不入待执行队列；init_ptr 的 ID、计划和执行部分被覆盖。
 * @return 新任务的池内地址，下次任务池变化前有效。
 */
DominoTask* dominoHumanStoreAddTask(DominoHumanData* data_ptr, const DominoTask* init_ptr);

/** @brief 整体替换任务的未来步骤并推进计划版本；count 为 0 即清空。
 * @pre item_list 不指向本任务的计划块。
 */
void dominoHumanStoreReplacePlan(DominoTask* task_ptr, const DominoBehaviorPlanItem* item_list, uint8_t count);

/** @brief 取出第 index 个计划项（启动时消费）并推进计划版本；其余步骤保持先后。 */
DominoBehaviorPlanItem dominoHumanStoreConsumePlanItem(DominoTask* task_ptr, uint8_t index);

/** @brief 分配行为 ID 建立执行记录，计入所属任务的未回收记录数；init_ptr 的 behavior_id 被覆盖。
 * @return 新记录的池内地址，下次行为池变化前有效。
 */
DominoBehavior* dominoHumanStoreAddBehavior(DominoTask* task_ptr, const DominoBehavior* init_ptr);

/** @brief 回收已终结并已归并的行为记录，只释放存储。 */
void dominoHumanStoreReclaimBehavior(DominoTask* task_ptr, domino_behavior_id_t behavior_id);

/** @brief 回收终态或已撤销的任务：释放计划块和最后一条行为记录，从人物任务清单和任务池移除。
 * @pre 任务没有当前行为，不是运行任务，也不在待执行队列中。
 */
void dominoHumanStoreReclaimTask(DominoHumanData* data_ptr, uint8_t task_index);

/** @brief 回收已终结且没有关联任务的动机，从人物动机清单和动机池移除。 */
void dominoHumanStoreReclaimMotivation(DominoHumanData* data_ptr, uint8_t motivation_index);

/** @brief 在待执行队列第 index 位插入 task_id（index 等于长度即追加）。
 * @pre 队列未满。
 */
void dominoHumanTaskQueueInsert(DominoHumanData* data_ptr, uint8_t index, domino_task_id_t task_id);

/** @brief 从待执行队列移除 task_id，保持其余顺序；不存在返回 false。 */
bool dominoHumanTaskQueueRemove(DominoHumanData* data_ptr, domino_task_id_t task_id);

#endif
