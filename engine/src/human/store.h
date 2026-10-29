#ifndef DOMINO_ENGINE_HUMAN_STORE_H
#define DOMINO_ENGINE_HUMAN_STORE_H

#include <stdbool.h>
#include <stddef.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "klib/kvec.h"

typedef kvec_t(DominoTask) DominoTaskVec;

extern DominoTaskVec domino_all_task_list;

/** @brief 创建任务存储；生命周期顺序由 human 模块保证。
 * @pre 首次使用静态空状态，再次使用前须完成 Destroy。
 * @note store 由加载阶段或 human 执行线程单独写入，不提供跨线程共享读写。
 */
void dominoHumanStoreInit(void);

/** @brief 释放任务行为块、任务池和 ID 索引并清空状态。
 * @pre 存储已 Init，引擎已 Stop 并完成存档；调用顺序由 human 模块保证。
 */
void dominoHumanStoreDestroy(void);

/** @brief 按 ID 借用任务池元素；扩容、删除或重排后须重新查询。
 * @pre 任务存储已 Init 且尚未 Destroy，由引擎保证所有内部查询和修改均在此期间执行。
 */
DominoTask* dominoGetTaskByID(domino_task_id_t task_id);
DominoBehavior* dominoGetBehaviorByTaskIDAndSequenceIndex(domino_task_id_t task_id, uint8_t sequence_index);

/** @brief 导入当前格式可信存档中的任务并接管行为块，执行版本初始化为 1。
 * @pre 任务存储已 Init，加载流程独占写入，尚未 Destroy；task_ptr 非空、身份有效、ID 唯一且独占非空行为块。
 * @note 输入任务须独占行为块；失败保持调用方所有权，成功后调用方须清空自己的行为指针，不再访问或释放该块。
 * 存档保证任务及行为语义、人物清单和跨任务关系正确；此处仅构造本次实例的任务池和 ID 索引。
 * 不修改人物任务清单；加载时清单来自人物存档，运行期通过 dominoHumanStoreAddValidatedTask() 提交已校验草案并追加 ID。
 * @return CODE_OK 成功；任务池容量不足返回 ERR_CAPACITY_EXCEEDED。
 * @note 返回的业务结果不打印日志，由调用方结合上下文处理；内存分配失败直接打印并终止进程。
 */
DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* task_ptr);
/** @brief 提交已校验的无 ID PLANNED 草案，按提交顺序追加到人物任务数组；成功接管行为块。
 * @pre 任务存储已 Init，调用方独占任务池与人物清单写入权，尚未 Destroy。
 * @note 参数均非空；受控构造器保证草案完整语义，调用方已确认当前目标建筑及归属，data_ptr 为 owner 已加载绑定的有效任务清单。
 * draft_ptr 不能借用任务池，须独占行为块；不带父子/停止关系，任务及行为尚未启动且有效进度为零。
 * 可信存档及 store 增删入口保证已有任务的存在性和归属；本接口负责容量、版本上限、取号及入池资源错误。
 * 从全局原子计数器取号，回绕时跳过 0 和仍在任务池中的 ID；失败允许消耗候选 ID，不回退计数器。
 * 成功后池内 execution_version 初始化为 1，调用方须清空行为指针；失败保持调用方所有权。
 * @return CODE_OK 成功；人物清单或任务池容量不足返回 ERR_CAPACITY_EXCEEDED；日程版本溢出返回 ERR_OUT_OF_RANGE。
 * @note 返回的业务结果不打印日志，由调用方结合上下文处理；内存分配失败直接打印并终止进程。
 */
DOMINO_CODE dominoHumanStoreAddValidatedTask(const DominoTask* draft_ptr, DominoHumanData* data_ptr, domino_task_id_t* task_id_ptr);
/** @brief 回收无父子／交接引用的终态任务，从人物数组稳定移除并清理自身活动槽位。
 * @pre 任务存储已 Init，调用方独占任务池与人物清单写入权，尚未 Destroy。
 * @note 仅操作有效任务清单；可信存档及 store 增删入口保证清单容量、任务存在性和归属。
 * 调用前须完成业务资源清理，不会取消或结束其他任务。
 * @return CODE_OK 成功；任务不存在返回 ERR_NOT_FOUND；仍在执行或被引用返回 ERR_GAME_STATE_INVALID；日程版本溢出返回 ERR_OUT_OF_RANGE。
 * @note 返回的业务结果不打印日志，由调用方结合上下文处理；已入池任务归属损坏直接打印并终止进程。
 */
DOMINO_CODE dominoHumanStoreRemoveTask(domino_task_id_t task_id);

/** @brief 按人物清单统一校验任务及行为、唯一归属、父子/待交接关系、三通道及时间。
 * @pre 任务存储已 Init 且尚未 Destroy，无并发修改。
 * @note 用于保存前统一审计；调用前由 storage 检查人物/data 同序身份、ID 索引及任务清单容量。
 *       世界事实不一致均为致命错误，直接打印并终止进程；可信加载和 Start 不重复全量审计。
 */
void dominoHumanStoreValidate(domino_runtime_ms_t now_ms);
/** @brief 可信加载完成后清空人物调度运行态；保留任务自身的执行版本及持久事实，不执行业务动作。 */
void dominoHumanStoreResetRuntime(void);

#endif
