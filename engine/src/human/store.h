#ifndef DOMINO_ENGINE_HUMAN_STORE_H
#define DOMINO_ENGINE_HUMAN_STORE_H

#include <stdbool.h>
#include <stddef.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "klib/kvec.h"

typedef kvec_t(DominoTask) DominoTaskVec;

extern DominoTaskVec domino_all_task_list;

/** @brief store 由加载阶段或 human 执行线程单独写入；不提供跨线程共享读写。 */
DOMINO_CODE dominoHumanStoreInit(void);
void dominoHumanStoreExit(void);

/** @brief 按 ID 借用任务池元素；扩容、删除或重排后须重新查询。 */
DominoTask* dominoGetTaskByID(domino_task_id_t task_id);
DominoBehavior* dominoGetBehaviorByTaskIDAndSequenceIndex(domino_task_id_t task_id, uint8_t sequence_index);

/** @brief 导入已解析的任务并接管行为块；仅检查入池所需的 ID、行为块及容量，执行版本初始化为 1。
 * @note 输入任务须独占行为块；失败保持调用方所有权，成功后调用方须清空自己的行为指针，不再访问或释放该块。
 * 批量加载后须通过 dominoHumanStoreValidate() 的完整语义与关系校验才能绑定或使用；不逐条重复检查任务及行为状态。
 * 不修改人物任务清单；加载时清单来自人物存档，运行期通过 dominoHumanStoreAddValidatedTask() 提交已校验草案并追加 ID。
 * @return CODE_OK 成功；store 未初始化返回 ERR_NOT_INITIALIZED；空任务参数返回 ERR_NULL_POINTER；
 * 缺少身份、行为块或行为数量为零的任务记录返回 ERR_INVALID_DATA；ID 重复返回 ERR_ALREADY_EXISTS；
 * 任务池容量不足返回 ERR_CAPACITY_EXCEEDED；实际分配失败返回 ERR_MEMORY_ALLOC。
 */
DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr);
/** @brief 提交已校验的无 ID PLANNED 草案，按提交顺序追加到人物任务数组；成功接管行为块。
 * @note 参数均非空；调用方已完成草案完整语义、目标建筑及归属校验，data_ptr 为 owner 已加载绑定的有效任务清单。
 * draft_ptr 不能借用任务池，须独占行为块；不带父子/停止关系，任务及行为尚未启动且有效进度为零。
 * 加载校验及 store 增删入口保证已有任务的存在性和归属；本接口负责容量、版本上限、取号及入池资源错误。
 * 从全局原子计数器取号，回绕时跳过 0 和仍在任务池中的 ID；失败允许消耗候选 ID，不回退计数器。
 * 成功后池内 execution_version 初始化为 1，调用方须清空行为指针；失败保持调用方所有权。
 * @return CODE_OK 成功；人物清单或任务池容量不足返回 ERR_CAPACITY_EXCEEDED；日程版本溢出返回 ERR_OUT_OF_RANGE；
 * 实际分配失败返回 ERR_MEMORY_ALLOC；store 未初始化返回 ERR_NOT_INITIALIZED。
 */
DOMINO_CODE dominoHumanStoreAddValidatedTask(const DominoTask* draft_ptr, DominoHumanData* data_ptr, domino_task_id_t* task_id_ptr);
/** @brief 回收无父子／交接引用的终态任务，从人物数组稳定移除并清理自身活动槽位。
 * @note 仅操作有效任务清单；加载校验及 store 增删入口保证清单容量、任务存在性和归属。
 * 调用前须完成业务资源清理，不会取消或结束其他任务。
 * @return CODE_OK 成功；store 未初始化返回 ERR_NOT_INITIALIZED；任务不存在返回 ERR_NOT_FOUND；
 * 仍在执行或被引用返回 ERR_GAME_STATE_INVALID；任务归属损坏返回 ERR_INVALID_DATA；日程版本溢出返回 ERR_OUT_OF_RANGE。
 */
DOMINO_CODE dominoHumanStoreRemoveTask(domino_task_id_t task_id);

/** @brief 按人物清单统一校验任务及行为、唯一归属、父子/待交接关系、三通道及时间。
 * @note 调用前须校验人物/data 同序身份、ID 索引及任务清单容量；加载和保存由 storage 的统一入口保证。
 */
DOMINO_CODE dominoHumanStoreValidate(domino_runtime_ms_t now_ms);
/** @brief 已校验加载事实后清空人物调度运行态；保留任务自身的执行版本及持久事实，不执行业务动作。 */
void dominoHumanStoreResetRuntime(void);

#endif
