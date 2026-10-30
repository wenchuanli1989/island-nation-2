#ifndef DOMINO_ENGINE_HUMAN_TASK_SLEEP_H
#define DOMINO_ENGINE_HUMAN_TASK_SLEEP_H

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "draft.h"

/** @file sleep.h
 * @brief 单个核心睡眠行为的同步内核；复用现有任务、行为和人物需求，不读取时钟或登记计时器。
 * @note 访问世界的入口调用前须完成模块初始化、可信存档加载及人物 data 绑定；调用方独占相关人物和任务的写入。
 * human_ptr 与已提交 task_ptr 由调用方在同次操作中取得，须属于同一人物；不得跨数组扩容、删除或重排保留借用指针。
 * 已提交任务及需求记录的完整语义由可信存档和受控写入维持；Start/Advance 只检查本次支持形态、业务条件及时间。
 * now_ms 使用同一世界累计未冻结现实毫秒，调用顺序不得倒退，0 与 UINT32_MAX 均可作为时刻。
 * 睡眠不依赖建筑或地点许可。
 * 首版只执行单行为睡眠，允许选择任一通道，但不与其他活动任务并行。
 * 异常人物拒绝提交、启动和推进；参数、资格和记录校验仅返回业务结果，由调用方决定诊断和错误处理；分配失败在检测处直接终止进程。
 */

/** @brief 从值快照构造单行为睡眠草案，不读取世界、不分配任务 ID、不修改需求或日程。
 * @param human_id 快照所属人物的非零 ID。
 * @param channel_index 三个通道之一。
 * @param energy snapshot_ms 时的预测精力，须在 0..100 且不高于睡眠触发阈值。
 * @param snapshot_ms 计划开始的累计未冻结现实毫秒。
 * @param draft_out_ptr 成功时写入无 ID 的 PLANNED 草案；调用方不得以仍持有行为块的草案作为输出。
 * @return CODE_OK 成功；空输出返回 ERR_NULL_POINTER；非法 ID/通道返回 ERR_INVALID_PARAM；精力值或到期时刻越界返回
 * ERR_OUT_OF_RANGE；未达到睡眠触发条件返回 ERR_GAME_STATE_INVALID；分配失败直接打印致命错误并终止进程。
 * @note 成功后调用方独占草案行为块，store 提交只复制内容，提交后仍须释放；失败保持输出不变。
 * 本函数仅校验值输入，不证明人物或通道目前仍有效。
 */
DOMINO_CODE dominoHumanBuildSleepTaskDraft(domino_human_id_t human_id, uint8_t channel_index, uint8_t energy, domino_runtime_ms_t snapshot_ms,
                                           DominoTaskDraft* draft_out_ptr);

/** @brief 精力不高于触发阈值时提交 PLANNED 睡眠任务，不立即启动或修改需求。
 * @param human_ptr 调用方已取得且 data 已绑定的正常人物。
 * @param channel_index 三个通道之一。
 * @param now_ms 预测自然需求并构造计划的时刻。
 * @param task_id_out_ptr 成功时写入新任务 ID；失败保持原值。
 * @return CODE_OK 成功；空参数返回 ERR_NULL_POINTER；非法通道或倒退时刻返回 ERR_INVALID_PARAM；资格改变返回
 * ERR_GAME_STATE_INVALID；已有未终结睡眠返回 ERR_ALREADY_EXISTS；任务容量耗尽返回
 * ERR_CAPACITY_EXCEEDED；记录损坏或时间范围错误直接传播；分配失败直接打印致命错误并终止进程。
 * @note 要求三个活动槽均为空，且本人没有未终结的睡眠任务；不自动回收终态任务。
 * store 按值复制草案行为，临时行为块由本接口释放；失败不修改人物、需求或任务清单。
 */
DOMINO_CODE dominoHumanSubmitSleepTask(DominoHuman* human_ptr, uint8_t channel_index, domino_runtime_ms_t now_ms, domino_task_id_t* task_id_out_ptr);

/** @brief 从 now_ms 首次激活已提交的 PLANNED 睡眠，结算启动前自然需求，并占用其通道。
 * @param task_ptr 调用方已取得的已提交、尚未启动的单行为睡眠任务。
 * @param human_ptr 任务所属的正常人物，data 已绑定。
 * @param now_ms 不早于任务和行为计划开始的实际启动时刻。
 * @return CODE_OK 成功；状态/资格不满足或尚未到计划开始时刻返回 ERR_GAME_STATE_INVALID；倒退至已结算时刻之前返回
 * ERR_INVALID_PARAM；时间或版本溢出返回 ERR_OUT_OF_RANGE；空参数结果直接传播。失败不修改需求、任务或槽位。
 * @note 三个活动槽均须为空；已满足精力目标时拒绝启动。按启动时需求重新估算目标时长，
 * 将本任务和行为计划修订为 [now_ms, now_ms + target_active_ms)，有变化才递增 schedule_revision。
 * 后续目标有效时长直接由行为计划区间长度推导，执行期间保持该长度不变。
 * 不调整其他任务，不处理日程冲突；实际进度从零开始，过去的计划时间不产生睡眠收益。
 */
DOMINO_CODE dominoHumanStartSleepTask(DominoTask* task_ptr, DominoHuman* human_ptr, domino_runtime_ms_t now_ms);

/** @brief 同步推进 RUNNING/ACTIVE 睡眠及人物自然需求，完成后清槽并保留终态任务。
 * @param task_ptr 调用方已取得的已启动独立睡眠任务；正常人物已成功的同类任务重复调用无效果。
 * @param human_ptr 任务所属的正常人物，data 已绑定。
 * @param now_ms 不早于行为 accounted_at_ms 的本次结算时刻。
 * @return CODE_OK 成功；不支持的状态或资格返回 ERR_GAME_STATE_INVALID；倒退至结算/结束时刻之前返回 ERR_INVALID_PARAM；
 * 计划区间、有效进度或需求结算记录不一致返回 ERR_INVALID_DATA；完成时版本溢出返回 ERR_OUT_OF_RANGE；空参数结果直接传播。
 * 所有错误均发生在效果写回前。
 * @note 可信加载后可从保存的进度继续结算；自然变化和每点恢复按时序结算，同刻先衰减后恢复。
 * 目标有效时长由行为 planned_end_ms - planned_begin_ms 推导，不随当前精力重新估算；
 * 有效睡眠最多累计至该目标，延迟处理时，其后的时间只自然衰减。完成记录处理时刻，递增一次 execution_version。
 * 任务终结后如需继续结算人物时间，应使用空闲需求入口或当时活动任务的结算入口，不能继续依靠本任务推进。
 * 不隐式启动后续任务，不重复 start；等待及多通道联合结算留待执行器接入。
 */
DOMINO_CODE dominoHumanAdvanceSleepTask(DominoTask* task_ptr, DominoHuman* human_ptr, domino_runtime_ms_t now_ms);

#endif
