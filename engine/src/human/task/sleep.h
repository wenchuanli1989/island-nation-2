#ifndef DOMINO_ENGINE_HUMAN_TASK_SLEEP_H
#define DOMINO_ENGINE_HUMAN_TASK_SLEEP_H

#include "../../queue/message.h"
#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"
#include "entry.h"

/** @file sleep.h
 * @brief 核心睡眠的规划和执行实现；由通用执行器管理通道、自然需求及计时器。
 * @note 睡眠固定使用主通道，同一人物同时最多一个活动睡眠任务。
 */

extern const DominoTaskOps domino_sleep_task_ops;

/** @brief 从值快照构造主通道的单行为睡眠草案，不读取世界、不分配任务 ID、不修改需求或日程。
 * @param human_id 快照所属人物的非零 ID。
 * @param energy 本次规划读取的精力，须在 0..100 且不高于睡眠触发阈值；提交时由执行器重新结算并复核。
 * @param snapshot_ms 计划开始的累计未冻结现实毫秒。
 * @param draft_out_ptr 成功时写入无 ID 的 PLANNED 草案；调用方不得以仍持有行为块的草案作为输出。
 * @return CODE_OK 成功；空输出返回 ERR_NULL_POINTER；非法 ID 返回 ERR_INVALID_PARAM；精力值或到期时刻越界返回
 * ERR_OUT_OF_RANGE；未达到睡眠触发条件返回 ERR_GAME_STATE_INVALID；分配失败直接打印致命错误并终止进程。
 * @note 成功后调用方独占草案行为块，store 提交只复制内容，提交后仍须释放；失败保持输出不变。
 * 本函数仅校验值输入，不证明人物目前仍有效或主通道可用。
 */
DOMINO_CODE dominoHumanBuildSleepTaskDraft(domino_human_id_t human_id, uint8_t energy, domino_runtime_ms_t snapshot_ms,
                                           DominoTaskDraft* draft_out_ptr);

#endif
