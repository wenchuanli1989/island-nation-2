#ifndef DOMINO_ENGINE_HUMAN_STORE_H
#define DOMINO_ENGINE_HUMAN_STORE_H

#include <stdbool.h>
#include <stddef.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "klib/kvec.h"

typedef kvec_t(DominoTask) DominoTaskVec;
typedef kvec_t(DominoBehavior) DominoBehaviorVec;

extern DominoTaskVec domino_all_task_list;
extern DominoBehaviorVec domino_all_behavior_list;

/** @brief 创建任务及行为存储；生命周期顺序由 human 模块保证。
 * @pre 首次使用静态空状态，再次使用前须完成 Destroy。
 * @note store 由加载阶段或 human 执行线程单独写入，不提供跨线程共享读写。
 */
void dominoHumanStoreInit(void);

/** @brief 释放任务池、行为池和各自 ID 索引并清空状态。
 * @pre 存储已 Init，引擎已 Stop 并完成存档；调用顺序由 human 模块保证。
 */
void dominoHumanStoreDestroy(void);

/** @brief 按 ID 借用任务池元素；扩容、删除或重排后须重新查询。
 * @pre 任务存储已 Init 且尚未 Destroy，由引擎保证所有内部查询和修改均在此期间执行。
 */
DominoTask* dominoGetTaskByID(domino_task_id_t task_id);

/** @brief 按 ID 借用全局行为池元素；行为池扩容、删除或重排后须重新查询。
 * @pre 与任务查询相同，由调用方保证 store 有效且不与其他写入并发。
 */
DominoBehavior* dominoGetBehaviorByID(domino_behavior_id_t behavior_id);

/** @brief 按值导入当前格式可信存档中的任务，执行版本初始化为 1。
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

/** @brief 提交已校验的无 ID PLANNED 草案，复制行为进全局池，按提交顺序追加到人物任务数组。
 * @pre store 已 Init，调用方独占任务、行为池与人物清单写入权，尚未 Destroy。
 * @note 参数均非空；受控构造器保证草案完整语义，调用方已确认人物归属，data_ptr 为 owner 已加载绑定的有效任务清单。
 * draft_ptr 与 behaviors_ptr 不能借用全局池，后者提供 behavior_count 项自有行为副本；任务及行为尚未启动且有效进度为零。
 * 可信存档及 store 增删入口保证已有任务和行为的存在性及归属；本接口负责容量、版本上限、取号及入池资源错误。
 * 从全局原子计数器为任务与行为各自取号，回绕时跳过 0 和仍在相应池中的 ID；失败允许消耗候选 ID，不回退计数器。
 * 成功后池内 execution_version 初始化为 1，current_behavior_id 指向首个行为；调用方始终保有并负责释放草案行为块。
 * @return CODE_OK 成功；人物清单、任务池或行为池容量不足返回 ERR_CAPACITY_EXCEEDED；日程版本溢出返回 ERR_OUT_OF_RANGE。
 * @note 返回的业务结果不打印日志，由调用方结合上下文处理；内存分配失败直接打印并终止进程。
 */
DOMINO_CODE dominoHumanStoreAddValidatedTask(const DominoTask* draft_ptr, const DominoBehavior* behaviors_ptr, DominoHumanData* data_ptr,
                                             domino_task_id_t* task_id_ptr);
/** @brief 回收终态任务及其全部全局行为，从人物数组稳定移除并清理自身活动槽位。
 * @pre 任务存储已 Init，调用方独占任务池与人物清单写入权，尚未 Destroy。
 * @note 仅操作有效任务清单；可信存档及 store 增删入口保证清单容量、任务存在性和归属。
 * 调用前须完成业务资源清理，不会取消或结束其他任务。
 * @return CODE_OK 成功；任务不存在返回 ERR_NOT_FOUND；尚未终结返回 ERR_GAME_STATE_INVALID；日程版本溢出返回 ERR_OUT_OF_RANGE。
 * @note 返回的业务结果不打印日志，由调用方结合上下文处理；已入池任务归属损坏直接打印并终止进程。
 */
DOMINO_CODE dominoHumanStoreRemoveTask(domino_task_id_t task_id);

/** @brief 可信加载完成后清空人物调度运行态；保留任务自身的执行版本及持久事实，不执行业务动作。 */
void dominoHumanStoreResetRuntime(void);

#endif
