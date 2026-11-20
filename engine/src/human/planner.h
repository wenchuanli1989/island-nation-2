#ifndef DOMINO_ENGINE_HUMAN_PLANNER_H
#define DOMINO_ENGINE_HUMAN_PLANNER_H

#include "../queue/message.h"
#include "domino_shared_queue.h"
#include "state.h"

/** @file planner.h
 * @brief 动机、任务、行为三类规划器：三个线程复用同一份私有循环，各自持有独立上下文和高／普通请求、结果四条队列。
 * @note 每批最多处理 K 个高优先级请求和一个普通请求，再回到批次边界检查退出和冻结。规划线程持 human 读锁核对请求身份并只读计算，
 * 结果在读锁外非阻塞投递到沿用原优先级的结果队列，再唤醒执行器收件箱。请求不设期限：只跳过身份不匹配的失效请求，
 * 有效请求必然得到一个结果。动机和任务规划器随结果给出下次规划时刻，由执行器并入人物唤醒；
 * 规划器不修改世界、不分配正式 ID、不登记计时器、不保存每人物的计时状态、不启动执行。
 */

/** @brief 单类规划器的性能统计；只由本类线程写入，join 后读取。 */
typedef struct DominoHumanPlannerStats {
    uint64_t batch_count;
    uint64_t request_count_list[DOMINO_HUMAN_PLAN_PRIORITY_COUNT];  ///< 已出队请求数（含失效丢弃）。
    uint64_t dropped_count;                                         ///< 请求身份不匹配（已失效）而丢弃的请求数。
    uint64_t busy_ns;                                               ///< 批次内请求核对、读锁访问、计算及投递的单调耗时合计。
    uint64_t max_request_ns;
    uint64_t max_batch_size;
} DominoHumanPlannerStats;

/** @brief 一次规划计算的输出；调用方传入零初始化的结构。 */
typedef struct DominoHumanPlanOutput {
    void* candidate_ptr;                  ///< 结果为 CANDIDATE 时为新分配的候选块，其他结果保持为空。
    domino_runtime_ms_t next_plan_at_ms;  ///< 下次规划时刻，须晚于规划时刻；仅 next_plan_valid_flag 为真时有效。
    bool next_plan_valid_flag;            ///< 动机、任务规划器按需给出；行为规划器恒为假。
} DominoHumanPlanOutput;

/** @brief 规划计算入口签名：在调用方持有的 human 读锁内只读计算，可分配自有候选块。
 * @return DOMINO_HUMAN_PLAN_OUTCOME；任何结果分类（包括 NO_PLAN）都可给出下次规划时刻。
 */
typedef uint8_t (*DominoHumanPlanComputeFn)(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                            uint32_t compute_budget, DominoHumanPlanOutput* output_ptr);

/** @brief 建立三类规划器的十二条队列和上下文，绑定执行器收件箱作为结果通知对象；不创建线程。 */
void dominoHumanPlannerInit(DominoThreadQueue* executor_inbox_ptr);

/** @brief 依次创建动机、任务、行为规划线程。 */
void dominoHumanPlannerStart(void);

/** @brief 时间或退出状态发布后，唤醒三个规划线程的统一等待对象（各自的普通请求队列）。 */
void dominoHumanPlannerNotifyTimeChanged(void);

/** @brief 依次 join 动机、任务、行为规划线程；保留全部队列及留队结果。
 * @pre 执行器已 join，不再投递请求。
 */
void dominoHumanPlannerStop(void);

/** @brief 清理留队结果（整块释放候选）、销毁十二条队列并复位上下文。
 * @pre 规划线程未启动或已 join，执行器不再访问结果队列。
 */
void dominoHumanPlannerDestroy(void);

/** @brief 执行器在 human 写锁内非阻塞投递规划请求，高优先级成功入队后唤醒该类普通请求队列。
 * @return 本请求队列生成的非零 request_id；投递失败致命退出。
 */
uint64_t dominoHumanPlannerPost(uint8_t planner_kind, uint8_t priority, domino_human_id_t human_id);

/** @brief 执行器消费的结果队列。 */
DominoThreadQueue* dominoHumanPlannerResultQueue(uint8_t planner_kind, uint8_t priority);

/** @brief 读取单类规划器统计；须在该线程 join 之后调用。 */
DominoHumanPlannerStats dominoHumanPlannerGetStats(uint8_t planner_kind);

/** @brief 三类最小睡眠策略的计算入口（policy.c）。 */
uint8_t dominoHumanPlanMotivations(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                   uint32_t compute_budget, DominoHumanPlanOutput* output_ptr);
uint8_t dominoHumanPlanTasks(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms, uint32_t compute_budget,
                             DominoHumanPlanOutput* output_ptr);
uint8_t dominoHumanPlanBehaviors(const DominoHuman* human_ptr, const DominoHumanPlanSlot* slot_ptr, domino_runtime_ms_t now_ms,
                                 uint32_t compute_budget, DominoHumanPlanOutput* output_ptr);

#ifdef DOMINO_PLAYGROUND_MODE
/** @brief 仅 playground：为某类规划器安装受控规划入口（空指针恢复默认策略）；须在 Start 前设置。 */
void dominoHumanPlannerSetComputeOverride(uint8_t planner_kind, DominoHumanPlanComputeFn compute_fn);
#endif

#endif
