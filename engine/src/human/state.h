#ifndef DOMINO_ENGINE_HUMAN_STATE_H
#define DOMINO_ENGINE_HUMAN_STATE_H

#include "../queue/message.h"
#include "domino_shared_rwlock.h"
#include "entry.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @file state.h
 * @brief human 内部可写存储；仅执行器调用链和串行加载、结算、保存、销毁使用。
 * @note 运行期由执行器持写锁修改，内部不重复加锁；规划线程经 entry.h 取得读锁后只读。
 * 一把锁同时保护 Human、Data、运行数组、Motivation、Task、Behavior、容器布局及 ID 索引；冻结和 STOPPING 末批仍遵循此约定。
 * 借用指针不能跨容器扩容、删除或重排保存。
 */
KHASHL_MAP_INIT(KH_LOCAL, DominoHumanIDMap, dominoHumanIdMap, domino_human_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

/** @brief 每人物一类规划器的有效请求记录；request_id 为 0 表示没有有效请求，其余字段此时不参与判断。
 * @note 人物来自所属记录、类别来自槽位；只保存请求 ID、优先级及触发范围。请求不设期限，每个有效请求在本会话内必然得到一个结果。
 */
typedef struct DominoHumanPlanSlot {
    uint64_t request_id;
    uint32_t scope_id;
    uint8_t priority;    ///< DOMINO_HUMAN_PLAN_PRIORITY。
    uint8_t scope_kind;  ///< DOMINO_HUMAN_PLAN_SCOPE。
} DominoHumanPlanSlot;

/** @brief 动机、任务两类规划在运行态位集合中的位；行为规划仍按任务扫描触发，不使用这些位。 */
enum {
    DOMINO_HUMAN_PLAN_MASK_MOTIVATION = 1U << DOMINO_HUMAN_PLANNER_MOTIVATION,
    DOMINO_HUMAN_PLAN_MASK_TASK = 1U << DOMINO_HUMAN_PLANNER_TASK,
    DOMINO_HUMAN_PLAN_MASK_ALL = DOMINO_HUMAN_PLAN_MASK_MOTIVATION | DOMINO_HUMAN_PLAN_MASK_TASK,
};

/** @brief 人物运行态，与人物数组同序，不持久化；Init 后由 InitAfter 建立，Destroy 释放。
 * @note 待规划标志由事件置位、投递请求时清除，结果不改变标志；下次规划时刻由身份匹配的结果覆盖，到点后清除并置相应标志。
 */
typedef struct DominoHumanRuntime {
    DominoHumanPlanSlot slot_list[DOMINO_HUMAN_PLANNER_COUNT];
    uint64_t posted_batch_seq_list[DOMINO_HUMAN_PLANNER_COUNT];            ///< 最近一次投递所在的执行器批次，每批每类至多投递一次。
    uint64_t queue_version;                                                ///< 待执行队列版本，仅队列实际变化时递增。
    uint64_t wakeup_game_timer_id;                                         ///< 唯一有效唤醒登记，0 表示未登记。
    domino_runtime_ms_t wakeup_due_ms;                                     ///< 仅 wakeup_game_timer_id 非零时有效。
    domino_runtime_ms_t next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_COUNT];  ///< 动机、任务的下次规划时刻，next_plan_mask 对应位有效时使用。
    uint8_t pending_mask;                                                  ///< 待规划标志：动机待评估、任务待规划（DOMINO_HUMAN_PLAN_MASK_*）。
    uint8_t next_plan_mask;                                                ///< 下次规划时刻有效位（DOMINO_HUMAN_PLAN_MASK_*）。
    bool todo_flag;                                                        ///< 已在本地待办队列中。
} DominoHumanRuntime;

typedef kvec_t(DominoHuman) DominoHumanVec;
typedef kvec_t(DominoHumanData) DominoHumanDataVec;
typedef kvec_t(DominoMotivation) DominoMotivationVec;
typedef kvec_t(DominoTask) DominoTaskVec;
typedef kvec_t(DominoBehavior) DominoBehaviorVec;

extern DominoRWLock domino_human_data_lock;
extern DominoHumanVec domino_all_human_list;
extern DominoHumanDataVec domino_all_human_data_list;
extern DominoHumanIDMap* dominoHumanIdMap;
extern DominoMotivationVec domino_all_motivation_list;
extern DominoTaskVec domino_all_task_list;
extern DominoBehaviorVec domino_all_behavior_list;

/** @brief 与人物数组同序的运行数组；InitAfter 前为空。 */
extern DominoHumanRuntime* domino_human_runtime_list;

/** @brief 人物在全局数组中的下标。 */
static inline uint32_t dominoHumanIndexOf(const DominoHuman* human_ptr) {
    return (uint32_t)(human_ptr - domino_all_human_list.a);
}

/** @brief 同序运行态；InitAfter 之后有效。 */
static inline DominoHumanRuntime* dominoHumanRuntimeOf(const DominoHuman* human_ptr) {
    return &domino_human_runtime_list[dominoHumanIndexOf(human_ptr)];
}

/** @brief 置动机待评估和／或任务待规划标志；运行数组尚未建立时（串行准备之前）没有调度可言，直接忽略。
 * @param plan_mask DOMINO_HUMAN_PLAN_MASK_* 的组合。
 */
static inline void dominoHumanMarkPlanPending(const DominoHuman* human_ptr, uint8_t plan_mask) {
    if (domino_human_runtime_list != nullptr) {
        dominoHumanRuntimeOf(human_ptr)->pending_mask |= plan_mask;
    }
}

#endif
