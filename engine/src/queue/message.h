#ifndef DOMINO_ENGINE_QUEUE_MESSAGE_H
#define DOMINO_ENGINE_QUEUE_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_human_task.h"
#include "domino_shared_queue.h"
#include "domino_shared_types.h"

/** @file message.h
 * @brief 引擎内部队列消息及其复用载荷；按计时登记、人物规划和人物执行用途命名。
 * @note 单类型队列直接保存相应请求结构；多类型队列使用 id + union payload + type。
 * 引擎对外消息定义于 domino_engine.h。
 * 内部消息不跨会话保存：计时登记／通知由持久化人物需求及任务／行为进度重建，未提交规划由人物需求重新生成；
 * 已提交任务及行为保存正式身份和进度，加载时不重放旧请求或草案，不重复结算已生效结果。
 */

/** @brief 64 位登记 ID，不是时间值；业务毫秒统一使用 32 位 domino_runtime_ms_t。 */
typedef uint64_t domino_game_timer_id_t;

/** @brief 计时登记请求；命令队列直接保存本结构，最小堆和到期通知复用同一记录。
 * @note 命令入队生成 game_timer_id；到期通知将本记录按值嵌入 DominoHumanExecutionMessage，外层 id 接收业务队列的新编号。
 * 不得将本记录原样二次入队，以免首字段的稳定计时器身份被覆盖。
 */
typedef struct DominoGameTimerRequestMessage {
    domino_game_timer_id_t game_timer_id;  ///< 必须为首字段，由登记命令队列写入；本次 Init 生命周期内的稳定取消凭据。
    DominoThreadQueue* event_queue_ptr;    ///< 业务方拥有的收件队列，元素类型为 DominoHumanExecutionMessage。
    uint64_t data_id;                      ///< 业务标识，原样按值传递；GameTimer 不解释其含义或持有业务对象。
    domino_runtime_ms_t due_ms;            ///< 到期时刻：累计未冻结现实时间达到此毫秒数时到期，即 now_ms >= due_ms。
    uint8_t event_type;
    bool cancelled_flag;  ///< 取消标志，登记接口初始化为 false，由 Cancel 按请求 ID 标记。
} DominoGameTimerRequestMessage;

static_assert(offsetof(DominoGameTimerRequestMessage, game_timer_id) == 0,
              "DominoGameTimerRequestMessage game_timer_id must be first for queue ID writeback");
static_assert(sizeof(DominoGameTimerRequestMessage) == 32, "DominoGameTimerRequestMessage size must be 32 bytes");

/** @brief 未提交的任务计划及自有行为副本；不携带正式任务 ID、执行状态或行为 ID 清单。
 * @note 结构体复制仅复制行为地址；跨线程成功投递后转移该块所有权，接收方负责释放。
 * store 提交按值复制行为，成功或失败均不接管草案行为块，调用方随后仍须释放。
 */
typedef struct DominoTaskDraft {
    DominoBehavior* behaviors_ptr;
    domino_human_id_t owner_human_id;
    domino_runtime_ms_t planned_begin_ms;
    domino_runtime_ms_t planned_end_ms;
    DOMINO_TASK_TYPE type;
    uint8_t channel_index;
    uint8_t behavior_count;
    uint8_t priority_class;
} DominoTaskDraft;

/** @brief 人物规划请求；规划器的单类型请求队列直接保存本结构，业务数据由规划器持 human 读锁查询。 */
typedef struct DominoHumanPlanRequestMessage {
    uint64_t request_id;  ///< 必须为首字段，由请求队列生成；结果载荷原样保留，不使用结果队列的外层 ID。
    domino_human_id_t human_id;
} DominoHumanPlanRequestMessage;

static_assert(offsetof(DominoHumanPlanRequestMessage, request_id) == 0,
              "DominoHumanPlanRequestMessage request_id must be first for queue ID writeback");

typedef enum DominoHumanPlanOutcome {
    DOMINO_HUMAN_PLAN_DRAFT,
    DOMINO_HUMAN_PLAN_TIME_RANGE,     ///< 草案时长超过剩余可表示业务时间；当前时钟仍有效。
    DOMINO_HUMAN_PLAN_STATE_CHANGED,  ///< 请求身份仍有效，但当前精力不再满足规划条件。
} DominoHumanPlanOutcome;

/** @brief 人物规划结果载荷，按值嵌入执行器的 PLAN_RESULT 消息；request 复用原规划请求并保留关联身份。
 * @note DRAFT 的 draft 独占另行分配的行为块，不引用全局行为池；其他结果的行为指针为空。
 * 结果入队成功后行为块归执行器；store 提交仅复制，执行器在消费或销毁留队结果时释放原块。
 */
typedef struct DominoHumanPlanResult {
    DominoTaskDraft draft;
    DominoHumanPlanRequestMessage request;
    DominoHumanPlanOutcome outcome;
} DominoHumanPlanResult;

/** @brief 人物执行器收件消息的类型，由消费者据此选择有效载荷。 */
typedef enum DominoHumanExecutionMessageType {
    DOMINO_HUMAN_EXECUTION_MESSAGE_NONE = 0,
    DOMINO_HUMAN_EXECUTION_MESSAGE_GAME_TIMER,
    DOMINO_HUMAN_EXECUTION_MESSAGE_PLAN_RESULT,
} DominoHumanExecutionMessageType;

/** @brief 人物执行器的混合收件消息，同时接收规划结果和计时到期通知。
 * @note id 必须位于首字段，供通用队列回写；队列按值复制整条消息。
 * type 决定 payload 的有效成员；发送者直接初始化对应成员，无需分配或释放外层载荷。
 * 规划草案中的行为块仍为独占资源，入队成功转移所有权，失败仍归发送者；消费者处理或丢弃后按业务协议释放。
 * 载荷内部指针不会递归复制，不得指向发送者栈或可移动容器元素；game_timer 的队列指针由业务方保证生命周期。
 */
typedef struct DominoHumanExecutionMessage {
    uint64_t id;  ///< 所属收件队列回写的消息 ID，与载荷内部的业务 ID 独立。
    union {
        DominoHumanPlanResult plan;
        DominoGameTimerRequestMessage game_timer;
    } payload;
    DominoHumanExecutionMessageType type;
} DominoHumanExecutionMessage;

static_assert(offsetof(DominoHumanExecutionMessage, id) == 0, "DominoHumanExecutionMessage id must be first for queue ID writeback");

#endif
