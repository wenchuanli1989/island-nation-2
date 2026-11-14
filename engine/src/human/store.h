#ifndef DOMINO_HUMAN_STORE_H
#define DOMINO_HUMAN_STORE_H
#include "entry.h"
#include "metrics.h"
typedef struct DominoPlanCopyMetrics {
    DominoTimingSamples timing;
    uint64_t bytes;
} DominoPlanCopyMetrics;
/** @brief 全部线程结束后读取计划值复制及其分配耗时；Destroy 复位。 */
DominoPlanCopyMetrics dominoHumanStoreCopyMetrics(void);
/** @brief 串行或持写锁访问。导入只用于可信存档；新工作只能由规划候选接受入口调用 Add。 */
void dominoHumanStoreInit(void);
void dominoHumanStoreDestroy(void);
DOMINO_CODE dominoHumanStoreImportMotivation(const DominoMotivation* value);
DOMINO_CODE dominoHumanStoreImportTask(const DominoTask* value);
DOMINO_CODE dominoHumanStoreImportBehavior(const DominoBehavior* value);
domino_motivation_id_t dominoHumanStoreAddMotivation(DominoHumanData* data, DominoMotivation value);
domino_task_id_t dominoHumanStoreAddTask(DominoHumanData* data, DominoTask value);
domino_behavior_id_t dominoHumanStoreAddBehavior(DominoTask* task, DominoBehavior value);
/** @brief 先释放旧计划再复制已预检的独立输入值，实际分配失败致命；更新版本且不接管输入块。 */
void dominoHumanStoreReplacePlans(DominoTask* task, const DominoBehaviorPlan* plans, uint16_t count);
/** @brief 任务安排或已解析依赖变化；不随执行进度增加。 */
void dominoHumanStoreAdvanceArrangement(DominoTask* task);
void dominoHumanStoreConsumePlan(DominoTask* task);
/** @brief 仅清理已归并且无外部引用的终态行为；不提交任何业务效果。 */
void dominoHumanStoreRecycleBehaviors(DominoTask* task);
/** @brief 已终结或合法撤销且依赖已解除的任务；调用前移除全部通道引用。 */
void dominoHumanStoreRecycleTask(DominoHumanData* data, uint8_t index);
size_t dominoHumanStorePlanCount(void);
#endif
