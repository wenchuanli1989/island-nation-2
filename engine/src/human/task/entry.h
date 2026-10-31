#ifndef DOMINO_ENGINE_HUMAN_TASK_ENTRY_H
#define DOMINO_ENGINE_HUMAN_TASK_ENTRY_H

#include "domino_shared_error_codes.h"
#include "domino_shared_human.h"

/** @brief 任务执行接口；执行器统一管理通道、自然需求和唤醒，回调只修改本任务及其业务效果。
 * @note start 在通道空闲、同类型未运行且需求已结算到 now_ms 后调用；成功后由执行器设置 RUNNING 和占槽。
 * advance 按时间顺序调用；自然需求已结算到 now_ms，回调不得再次结算自然需求或修改其他任务、活动槽。
 * 完成时回调设置 SUCCEEDED，维护自身行为、结束时刻和执行版本，执行器释放对应槽位。
 * 业务失败通过错误码返回，由执行器记录 FAILED 并隔离人物；回调不改任务归属、类型或所属通道。
 * get_times 分别返回下一个效果结算边界和需要通知执行器的时刻；前者必须晚于本任务已结算时刻。
 * 结算边界只用于一次处理内合并各任务的效果，不为每个边界创建计时器；0 和 UINT32_MAX 均为合法时刻。
 * 回调不得重入执行器、阻塞等待或增删任务／行为池；池内记录在本次处理期间保持有效。
 */
typedef struct DominoTaskOps {
    bool (*supports)(const DominoTask* task_ptr);
    DOMINO_CODE (*start)(DominoTask* task_ptr, DominoHuman* human_ptr, domino_runtime_ms_t now_ms);
    DOMINO_CODE (*advance)(DominoTask* task_ptr, DominoHuman* human_ptr, domino_runtime_ms_t now_ms);
    void (*get_times)(const DominoTask* task_ptr, domino_runtime_ms_t* settle_ms_ptr, domino_runtime_ms_t* wakeup_ms_ptr);
} DominoTaskOps;

/** @brief 任务类型名称及已实现的执行接口；ops 为空的类型仍可保存，但不能启动。 */
typedef struct DominoTaskTypeInfo {
    const char* name_str;
    const DominoTaskOps* ops;
} DominoTaskTypeInfo;

const DominoTaskTypeInfo* dominoHumanGetTaskTypeInfo(DOMINO_TASK_TYPE type);

#endif
