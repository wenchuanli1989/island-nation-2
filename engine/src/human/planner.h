#ifndef DOMINO_ENGINE_HUMAN_PLANNER_H
#define DOMINO_ENGINE_HUMAN_PLANNER_H

#include "../queue/message.h"
#include "domino_shared_queue.h"
#include "state.h"

/** @file planner.h
 * @brief 动机、任务、行为三类规划器：三个线程共用同一循环，各有高／普通优先级的请求队列和结果队列。
 * @note 每批至多处理 high_quota 个高优先级请求加 1 个普通请求。规划线程持 human 读锁核对请求身份并只读计算，
 * 解锁后把结果投递到同优先级的结果队列再唤醒执行器。只丢弃身份已失效的请求，有效请求必然得到结果。
 * 规划器不修改世界、不分配正式 ID、不登记计时器。
 */

/** @brief 单类规划器的性能统计；只由该线程写入，join 后读取。 */
typedef struct DominoHumanPlannerStats {
    uint64_t batch_count;
    uint64_t request_count_list[DOMINO_HUMAN_PLAN_PRIORITY_COUNT];  ///< 出队的请求数（含丢弃的）。
    uint64_t dropped_count;                                         ///< 身份已失效而丢弃的请求数。
    uint64_t busy_ns;                                               ///< 处理有效请求的累计耗时。
    uint64_t max_request_ns;
    uint64_t max_batch_size;
} DominoHumanPlannerStats;

/** @brief 一次规划计算的输出；调用方传入零初始化的结构。 */
typedef struct DominoHumanPlanOutput {
    void* candidate_ptr;                  ///< 结果为 CANDIDATE 时为新分配的候选块，否则为空。
    domino_runtime_ms_t next_plan_at_ms;  ///< 下次规划时刻，须晚于规划时刻；next_plan_valid_flag 为真时有效。
    bool next_plan_valid_flag;            ///< 动机、任务规划器按需给出；行为规划器恒为假。
} DominoHumanPlanOutput;

/** @brief 规划计算函数：在调用方持有的读锁内只读计算，可分配候选块。
 * @return DOMINO_HUMAN_PLAN_OUTCOME；任何结果都可附带下次规划时刻。
 */
typedef uint8_t (*DominoHumanPlanComputeFn)(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                            uint32_t compute_budget, DominoHumanPlanOutput* output_ptr);

/** @brief 建立三类规划器的队列和上下文，记下用于通知结果的执行器收件箱；不创建线程。 */
void dominoHumanPlannerInit(DominoThreadQueue* executor_inbox_ptr);

/** @brief 依次创建动机、任务、行为规划线程。 */
void dominoHumanPlannerStart(void);

/** @brief 唤醒三个规划线程（各自等待在普通请求队列上）。 */
void dominoHumanPlannerNotifyTimeChanged(void);

/** @brief join 三个规划线程，保留全部队列。
 * @pre 执行器已 join，不再投递请求。
 */
void dominoHumanPlannerStop(void);

/** @brief 释放留队结果中的候选，销毁全部规划队列并复位。
 * @pre 规划线程未启动或已 join，执行器不再访问结果队列。
 */
void dominoHumanPlannerDestroy(void);

/** @brief 执行器在写锁内投递一条规划请求，返回请求队列生成的 request_id；投递失败致命退出。 */
uint64_t dominoHumanPlannerPostRequest(uint8_t planner_kind, uint8_t priority, domino_human_id_t human_id);

/** @brief 某类规划器在某优先级上的结果队列。 */
DominoThreadQueue* dominoHumanPlannerGetResultQueue(uint8_t planner_kind, uint8_t priority);

/** @brief 单类规划器统计；须在该线程 join 之后读取。 */
DominoHumanPlannerStats dominoHumanPlannerGetStats(uint8_t planner_kind);

/** @brief 第一阶段三类规划器的默认策略（policy.c）。 */
uint8_t dominoHumanPlanMotivations(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                   uint32_t compute_budget, DominoHumanPlanOutput* output_ptr);
uint8_t dominoHumanPlanTasks(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms, uint32_t compute_budget,
                             DominoHumanPlanOutput* output_ptr);
uint8_t dominoHumanPlanBehaviors(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                 uint32_t compute_budget, DominoHumanPlanOutput* output_ptr);

#ifdef DOMINO_PLAYGROUND_MODE
/** @brief 仅 playground：为某类规划器换用受控的计算函数（空指针恢复默认），须在 human Init 之前设置。 */
void dominoHumanPlannerSetComputeOverride(uint8_t planner_kind, DominoHumanPlanComputeFn compute_fn);
#endif

#endif
