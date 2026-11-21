#ifndef DOMINO_ENGINE_HUMAN_STATE_H
#define DOMINO_ENGINE_HUMAN_STATE_H

#include "../queue/message.h"
#include "domino_shared_rwlock.h"
#include "entry.h"
#include "klib/khashl.h"
#include "klib/kvec.h"

/** @file state.h
 * @brief human 的可写存储：人物容器、运行数组和读写锁；只供执行器调用链及串行的加载、结算、保存和销毁使用。
 * @note g_domino_human_lock 一把锁保护人物、Data、运行数组、动机、任务、行为及全部索引：运行期只有执行器持写锁修改，
 * 规划线程持读锁只读（宪法 9.2）。借用指针不能跨容器扩容、删除或重排保留。
 */
KHASHL_MAP_INIT(KH_LOCAL, DominoHumanIdMap, dominoHumanIdMap, domino_human_id_t, uint32_t, kh_hash_uint32, kh_eq_generic)

/** @brief 请求槽：人物在某类规划器上的唯一有效请求；request_id 为 0 表示没有。
 * @note 请求不设期限，每个有效请求在本会话内必然得到一个结果（宪法第六章）。
 */
typedef struct DominoHumanPlanSlot {
    uint64_t request_id;
    uint32_t scope_id;
    uint8_t priority;    ///< DOMINO_HUMAN_PLAN_PRIORITY。
    uint8_t scope_kind;  ///< DOMINO_HUMAN_PLAN_SCOPE。
} DominoHumanPlanSlot;

/** @brief 动机、任务规划在待规划标志和下次规划时刻位集合中的位；行为规划按任务扫描触发，不用这些位。 */
enum {
    DOMINO_HUMAN_PLAN_MASK_MOTIVATION = 1U << DOMINO_HUMAN_PLANNER_MOTIVATION,
    DOMINO_HUMAN_PLAN_MASK_TASK = 1U << DOMINO_HUMAN_PLANNER_TASK,
    DOMINO_HUMAN_PLAN_MASK_ALL = DOMINO_HUMAN_PLAN_MASK_MOTIVATION | DOMINO_HUMAN_PLAN_MASK_TASK,
};

/** @brief 人物运行态，与人物数组同序、不持久化；InitAfter 建立，Destroy 释放。
 * @note 待规划标志由事件置位、投递请求时清除；下次规划时刻由匹配的结果覆盖，到点后转为待规划标志。
 */
typedef struct DominoHumanRuntime {
    DominoHumanPlanSlot plan_slot_list[DOMINO_HUMAN_PLANNER_COUNT];
    uint64_t posted_batch_seq_list[DOMINO_HUMAN_PLANNER_COUNT];            ///< 最近一次投递所在的执行器批次；每批每类至多投递一次。
    uint64_t queue_version;                                                ///< 待执行队列版本，仅队列实际变化时递增。
    uint64_t wakeup_game_timer_id;                                         ///< 当前唯一有效的唤醒计时器，0 表示没有。
    domino_runtime_ms_t wakeup_due_ms;                                     ///< 仅 wakeup_game_timer_id 非零时有效。
    domino_runtime_ms_t next_plan_at_ms_list[DOMINO_HUMAN_PLANNER_COUNT];  ///< 动机、任务的下次规划时刻，next_plan_mask 对应位有效。
    uint8_t pending_mask;                                                  ///< 待规划标志（DOMINO_HUMAN_PLAN_MASK_*）。
    uint8_t next_plan_mask;                                                ///< 下次规划时刻有效位（DOMINO_HUMAN_PLAN_MASK_*）。
    bool todo_queued_flag;                                                 ///< 已在执行器的本地待办队列中。
} DominoHumanRuntime;

typedef kvec_t(DominoHuman) DominoHumanVec;
typedef kvec_t(DominoHumanData) DominoHumanDataVec;
typedef kvec_t(DominoMotivation) DominoMotivationVec;
typedef kvec_t(DominoTask) DominoTaskVec;
typedef kvec_t(DominoBehavior) DominoBehaviorVec;

extern DominoRWLock g_domino_human_lock;
extern DominoHumanVec g_domino_human_list;
extern DominoHumanDataVec g_domino_human_data_list;
extern DominoHumanIdMap* g_domino_human_id_map;
extern DominoMotivationVec g_domino_motivation_list;
extern DominoTaskVec g_domino_task_list;
extern DominoBehaviorVec g_domino_behavior_list;

/** @brief 与人物数组同序的运行数组；InitAfter 之前为空。 */
extern DominoHumanRuntime* g_domino_human_runtime_list;

/** @brief 人物在全局数组中的下标。 */
static inline uint32_t dominoHumanIndexOf(const DominoHuman* human_ptr) {
    return (uint32_t)(human_ptr - g_domino_human_list.a);
}

/** @brief 人物的运行态；InitAfter 之后有效。 */
static inline DominoHumanRuntime* dominoHumanRuntimeOf(const DominoHuman* human_ptr) {
    return &g_domino_human_runtime_list[dominoHumanIndexOf(human_ptr)];
}

/** @brief 置待规划标志（DOMINO_HUMAN_PLAN_MASK_* 组合）；运行数组尚未建立时忽略。 */
static inline void dominoHumanMarkPlanPending(const DominoHuman* human_ptr, uint8_t plan_mask) {
    if (g_domino_human_runtime_list != nullptr) {
        dominoHumanRuntimeOf(human_ptr)->pending_mask |= plan_mask;
    }
}

#endif
