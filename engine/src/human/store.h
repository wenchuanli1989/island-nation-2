#ifndef DOMINO_ENGINE_HUMAN_STORE_H
#define DOMINO_ENGINE_HUMAN_STORE_H

#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "task/draft.h"

/** @brief 创建任务及行为存储；生命周期顺序由 human 模块保证。
 * @pre 首次使用静态空状态，再次使用前须完成 Destroy。
 * @note 运行期仅执行器持 human 写锁修改；其他线程通过 entry.h 持读锁读取。串行加载、保存不加锁。
 */
void dominoHumanStoreInit(void);

/** @brief 释放任务池、行为池和各自 ID 索引并清空状态。
 * @pre 存储已 Init，引擎已 Stop 并完成存档；调用顺序由 human 模块保证。
 */
void dominoHumanStoreDestroy(void);

/** @brief 按值导入当前格式可信存档中的任务。
 * @pre 任务存储已 Init，加载流程独占写入，尚未 Destroy；task_ptr 非空、身份有效且 ID 唯一。
 * @note 存档保证任务与行为 ID 引用及人物清单正确；此处仅构造任务池和任务 ID 索引。
 * 不修改人物任务清单；加载时清单来自人物存档，运行期通过 dominoHumanStoreAddValidatedTask() 提交已校验草案并追加 ID。
 * @return CODE_OK 成功；任务池容量不足返回 ERR_CAPACITY_EXCEEDED。
 * @note 返回的业务结果不打印日志，由调用方结合上下文处理；内存分配失败直接打印并终止进程。
 */
DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr);

/** @brief 按值导入当前格式可信存档中的行为，并建立独立 ID 索引。
 * @pre store 已 Init，加载流程独占写入；behavior_ptr 非空，存档保证身份、ID 唯一及任务引用正确。
 * @return CODE_OK 成功；行为池容量不足返回 ERR_CAPACITY_EXCEEDED；内存分配失败直接打印并终止进程。
 */
DOMINO_CODE dominoHumanStoreImportBehavior(const DominoBehavior* behavior_ptr);

/** @brief 提交已校验的无 ID 草案，构造 PLANNED 任务并复制行为进全局池，按提交顺序追加到人物任务数组。
 * @pre store 已 Init，调用方独占任务、行为池与人物清单写入权，尚未 Destroy。
 * @note 参数均非空；受控构造器保证草案完整语义，调用方已确认人物归属，data_ptr 为 owner 已加载绑定的有效任务清单。
 * draft_ptr 及其 behaviors_ptr 不能借用全局池，后者提供 behavior_count 项自有行为副本；行为尚未启动且有效进度为零。
 * 可信存档及 store 增删入口保证已有任务和行为的存在性及归属；本接口负责容量、取号及入池资源错误。
 * 从全局原子计数器为任务与行为各自取号，回绕时跳过 0 和仍在相应池中的 ID；失败允许消耗候选 ID，不回退计数器。
 * 成功后 current_behavior_id 指向首个行为；调用方始终保有并负责释放草案行为块。
 * @return CODE_OK 成功；人物清单、任务池或行为池容量不足返回 ERR_CAPACITY_EXCEEDED。
 * @note 返回的业务结果不打印日志，由调用方结合上下文处理；内存分配失败直接打印并终止进程。
 */
DOMINO_CODE dominoHumanStoreAddValidatedTask(const DominoTaskDraft* draft_ptr, DominoHumanData* data_ptr);

/** @brief 回收已确认成功的任务及其全部全局行为，从人物任务清单稳定移除。
 * @pre store 已 Init；执行器持 human 写锁，或调用方处于串行阶段；尚未 Destroy。
 * @pre data_ptr 非空，task_index 位于有效清单内，对应任务为 SUCCEEDED，业务资源已清理且活动槽已清空。
 * @note 可信存档及 store 增删入口保证任务、行为存在且归属正确；删除会移动全局池，不跨此调用保留池内借用指针。
 */
void dominoHumanStoreRecycleTask(DominoHumanData* data_ptr, uint8_t task_index);

#endif
