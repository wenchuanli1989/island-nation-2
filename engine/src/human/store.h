#ifndef DOMINO_ENGINE_HUMAN_STORE_H
#define DOMINO_ENGINE_HUMAN_STORE_H

#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"

/** @file store.h
 * @brief human 内部动机／任务／行为池、ID 分配及人物清单维护；存储层可按本接口契约直接导入可信存档。
 * @note 运行期仅执行器持 human 写锁修改；加载、结算、保存和销毁串行直访不加锁。
 * 容量与业务条件由调用方的预检负责；本层只负责取号、入池、索引和真实资源失败（直接致命退出）。
 * 正式 ID 以存档世界内最后已分配值递增，耗尽致命退出，永不复用。删除采用交换删除，调用后不得保留池内借用指针。
 */

/** @brief 创建三个池的 ID 索引；首次使用静态空状态，再次使用前须完成 Destroy。 */
void dominoHumanStoreInit(void);

/** @brief 释放全部任务独占计划块、三个池及索引，复位计数和计划版本。 */
void dominoHumanStoreDestroy(void);

/** @brief 按值导入可信动机，建立 ID 索引；运行态字段由加载后的准备阶段重建。
 * @return CODE_OK；动机池达到上限返回 ERR_CAPACITY_EXCEEDED。
 */
DOMINO_CODE dominoHumanStoreImportMotivation(const DominoMotivation* motivation_ptr);

/** @brief 按值导入可信任务及其未来行为计划值；计划块由本层分配并独占。
 * @param task_ptr 解码后的任务，plan_item_list 字段被忽略。
 * @param plan_item_list plan_count 项计划值，可为空指针（plan_count 为 0 时）。
 * @return CODE_OK；任务池或计划项总量达到上限返回 ERR_CAPACITY_EXCEEDED。
 */
DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr, const DominoBehaviorPlanItem* plan_item_list, uint8_t plan_count);

/** @brief 按值导入可信行为记录并建立索引。
 * @return CODE_OK；行为记录池达到上限返回 ERR_CAPACITY_EXCEEDED。
 */
DOMINO_CODE dominoHumanStoreImportBehavior(const DominoBehavior* behavior_ptr);

/** @brief 当前各池数量及计划项总量，供容量预检。 */
uint32_t dominoHumanStoreMotivationCount(void);
uint32_t dominoHumanStoreTaskCount(void);
uint32_t dominoHumanStoreBehaviorCount(void);
uint32_t dominoHumanStorePlanItemCount(void);

/** @brief 取得本次 Init 内全局唯一、永不复用的非零计划版本；耗尽致命退出。 */
uint64_t dominoHumanStoreNextPlanVersion(void);

/** @brief 分配正式动机 ID 并入池，追加到人物动机清单。
 * @pre 调用方已检查人物及全局容量；init_ptr 的身份字段被覆盖。
 * @return 新动机的池内地址，仅在下一次动机池变化前有效。
 */
DominoMotivation* dominoHumanStoreAddMotivation(DominoHumanData* data_ptr, const DominoMotivation* init_ptr);

/** @brief 分配正式 task_id 建立未启动任务记录，追加到人物任务清单；不加入待执行队列。
 * @pre 调用方已检查人物、全局及计划容量；init_ptr 的 task_id、计划块和执行部分字段被覆盖。
 * @return 新任务的池内地址，仅在下一次任务池变化前有效。
 */
DominoTask* dominoHumanStoreAddTask(DominoHumanData* data_ptr, const DominoTask* init_ptr);

/** @brief 整体替换任务的未来行为序列并推进计划版本；count 为 0 表示显式清空。
 * @pre 调用方已检查单任务 255 上限及全局计划项容量；item_list 不借用本任务计划块。
 */
void dominoHumanStoreReplacePlan(DominoTask* task_ptr, const DominoBehaviorPlanItem* item_list, uint8_t count);

/** @brief 取出第 index 个计划项（启动时消费）并推进计划版本；其余步骤保持原有先后。
 * @pre index < plan_count。
 */
DominoBehaviorPlanItem dominoHumanStoreConsumePlanItem(DominoTask* task_ptr, uint8_t index);

/** @brief 分配正式行为 ID 建立执行记录，记入所属任务的未回收记录数。
 * @pre 调用方已检查行为记录池容量和单任务上限；init_ptr 的 behavior_id 被覆盖。
 * @return 新记录的池内地址，仅在下一次行为池变化前有效。
 */
DominoBehavior* dominoHumanStoreAddBehavior(DominoTask* task_ptr, const DominoBehavior* init_ptr);

/** @brief 回收已终结且必要事实已归并的行为记录，只释放存储，不补记任何业务事实。 */
void dominoHumanStoreReclaimBehavior(DominoTask* task_ptr, domino_behavior_id_t behavior_id);

/** @brief 回收终态或已撤销的任务：释放计划块和剩余行为记录，从人物任务清单稳定移除，再从任务池删除。
 * @pre 任务无当前行为、既不是运行任务也不在待执行队列中。
 */
void dominoHumanStoreRecycleTask(DominoHumanData* data_ptr, uint8_t task_index);

/** @brief 回收已终结且没有任何关联任务的动机：从人物动机清单稳定移除，再从动机池删除；ID 不复用。
 * @pre 动机不是 ACTIVE，人物任务清单中没有任务归属该动机。
 */
void dominoHumanStoreRecycleMotivation(DominoHumanData* data_ptr, uint8_t motivation_index);

/** @brief 在待执行队列的 index 位置插入 task_id；index 等于长度时追加。
 * @pre 队列未满且 index 不超过长度。
 */
void dominoHumanQueueInsert(DominoHumanData* data_ptr, uint8_t index, domino_task_id_t task_id);

/** @brief 从待执行队列稳定移除 task_id；不存在时返回 false。 */
bool dominoHumanQueueRemove(DominoHumanData* data_ptr, domino_task_id_t task_id);

#endif
