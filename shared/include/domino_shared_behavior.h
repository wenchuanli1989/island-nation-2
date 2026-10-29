#ifndef DOMINO_SHARED_BEHAVIOR_H
#define DOMINO_SHARED_BEHAVIOR_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_types.h"

/** @file domino_shared_behavior.h
 * @brief 任务与行为模型；任务直接持有行为块和执行版本，持久化时只写业务事实与行为内容。
 */

enum {
    DOMINO_HUMAN_TASK_CHANNEL_PRIMARY = 0,  ///< 主通道优先处理；三个通道均不限制任务类型。
    DOMINO_HUMAN_TASK_CHANNEL_SECONDARY_1 = 1,
    DOMINO_HUMAN_TASK_CHANNEL_SECONDARY_2 = 2,
    DOMINO_HUMAN_TASK_CHANNEL_COUNT = 3,
    DOMINO_HUMAN_TASK_COUNT_MAX = 32,
    DOMINO_HUMAN_TASK_CHAIN_DEPTH_MAX = 4,
    DOMINO_TASK_BEHAVIOR_COUNT_MAX = UINT8_MAX,
};

/** @brief 任务类型：人物打算完成哪件事；一个任务可以包含多个行为，例如上班包含通勤、工作和午餐。
 * @note 类型只描述业务含义，具体规划和执行逻辑需由对应任务实现。
 */
typedef enum {
    DOMINO_TASK_TYPE_NONE = 0,       ///< 尚未指定任务类型，不能作为有效任务提交。
    DOMINO_TASK_TYPE_WORK,           ///< 上班：围绕一次工作安排组织通勤、工作等步骤。
    DOMINO_TASK_TYPE_SCHOOL,         ///< 上学：围绕一次上学安排组织通勤、学习等步骤。
    DOMINO_TASK_TYPE_ENTERTAINMENT,  ///< 娱乐：安排看电影、玩游戏等休闲活动。
    DOMINO_TASK_TYPE_SHOPPING,       ///< 购物：前往购物地点并完成购买。
    DOMINO_TASK_TYPE_EXERCISE,       ///< 运动：安排跑步、健身等锻炼活动。
    DOMINO_TASK_TYPE_MEDICAL,        ///< 就医：前往医疗地点并接受诊治。
    DOMINO_TASK_TYPE_VISIT,          ///< 拜访：前往他人所在地点并进行交流。
    DOMINO_TASK_TYPE_TRAVEL,         ///< 旅游：安排出行、参观等活动，可包含过夜等子任务。
    DOMINO_TASK_TYPE_PARADE,         ///< 游行：参加一次集体游行活动。
    DOMINO_TASK_TYPE_SLEEP,          ///< 睡眠：以睡觉为核心，也可包含晚餐、洗漱和早餐。
    DOMINO_TASK_TYPE_PHONE_CALL,     ///< 通话：安排一次电话交流。
    DOMINO_TASK_TYPE_MESSAGE,        ///< 消息处理：安排一次接收、阅读或发送消息的操作。
    DOMINO_TASK_TYPE_COUNT,          ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_TYPE;

/** @brief 行为类型：任务中的一个具体步骤；例如上班任务中的移动和工作是两种行为。 */
typedef enum {
    DOMINO_BEHAVIOR_TYPE_NONE = 0,    ///< 尚未指定行为类型，不能作为有效行为使用。
    DOMINO_BEHAVIOR_TYPE_MOVE,        ///< 移动：从当前位置前往目标地点。
    DOMINO_BEHAVIOR_TYPE_EAT,         ///< 吃饭：执行一次进餐，早餐、午餐和晚餐共用此类型。
    DOMINO_BEHAVIOR_TYPE_WASH,        ///< 洗漱：执行一次清洁，睡前和起床洗漱共用此类型。
    DOMINO_BEHAVIOR_TYPE_WORK,        ///< 工作：实际进行劳动的步骤，不包含通勤等其他步骤。
    DOMINO_BEHAVIOR_TYPE_STUDY,       ///< 学习：实际上课或学习的步骤。
    DOMINO_BEHAVIOR_TYPE_SHOP,        ///< 购物：实际选购、付款等购买过程。
    DOMINO_BEHAVIOR_TYPE_EXERCISE,    ///< 运动：实际跑步、训练等，不是去健身地点的通勤。
    DOMINO_BEHAVIOR_TYPE_TREAT,       ///< 看病：实际接受检查或治疗的步骤。
    DOMINO_BEHAVIOR_TYPE_CHAT,        ///< 聊天：面对面交流。
    DOMINO_BEHAVIOR_TYPE_SIGHTSEE,    ///< 参观：在景点等地点游览。
    DOMINO_BEHAVIOR_TYPE_PARADE,      ///< 游行：实际参与集体游行的步骤。
    DOMINO_BEHAVIOR_TYPE_SLEEP,       ///< 睡觉：实际累计有效睡眠时长，不包含餐食和洗漱。
    DOMINO_BEHAVIOR_TYPE_ENTERTAIN,   ///< 娱乐：实际看电影、玩游戏等。
    DOMINO_BEHAVIOR_TYPE_PHONE_CALL,  ///< 通话：通过电话进行同步交流。
    DOMINO_BEHAVIOR_TYPE_MESSAGE,     ///< 消息处理：执行接收、阅读或发送之一；接收不等于已读或回复。
    DOMINO_BEHAVIOR_TYPE_COUNT,       ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_BEHAVIOR_TYPE;

/** @brief 任务状态：整件事进行到哪一步；SUCCEEDED、FAILED、CANCELLED 都表示已经结束。 */
typedef enum {
    DOMINO_TASK_STATUS_PLANNED = 0,  ///< 已列入安排，但还没有真正开始，不占活动通道。
    DOMINO_TASK_STATUS_RUNNING,      ///< 已开始并占用活动通道；当前行为可以正在执行、等待，或处理停止交接。
    DOMINO_TASK_STATUS_SUSPENDED,    ///< 暂时挂起，让同通道子任务先执行；子任务结束后再判断能否恢复。
    DOMINO_TASK_STATUS_SUCCEEDED,    ///< 任务成功结束，行为已完成或按规则跳过。
    DOMINO_TASK_STATUS_FAILED,       ///< 任务未能完成，具体失败原因记录在 result 中。
    DOMINO_TASK_STATUS_CANCELLED,    ///< 任务已被取消，不再继续；未开始的任务也可以取消。
    DOMINO_TASK_STATUS_COUNT,        ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_STATUS;

/** @brief 行为状态：当前这个步骤进行到哪一步；DONE、SKIPPED、FAILED、CANCELLED 都是终态。 */
typedef enum {
    DOMINO_BEHAVIOR_STATUS_PENDING = 0,  ///< 还没轮到或尚未激活这个步骤。
    DOMINO_BEHAVIOR_STATUS_ACTIVE,       ///< 正在执行，并累计该行为的有效进度。
    DOMINO_BEHAVIOR_STATUS_WAITING,      ///< 已启动，但正在等条件或时刻；只计算等待规则允许的成本，不算正常执行进度。
    DOMINO_BEHAVIOR_STATUS_PAUSED,       ///< 已完成安全停止并保留进度，等待恢复；不表示整个游戏被冻结。
    DOMINO_BEHAVIOR_STATUS_DONE,         ///< 这个步骤已经正常完成。
    DOMINO_BEHAVIOR_STATUS_SKIPPED,      ///< 尚未执行就按规则跳过，例如省略不需要的可选晚餐。
    DOMINO_BEHAVIOR_STATUS_FAILED,       ///< 这个步骤无法完成，已经以失败结束。
    DOMINO_BEHAVIOR_STATUS_CANCELLED,    ///< 这个步骤被取消；可能尚未开始，也可能已执行一部分。
    DOMINO_BEHAVIOR_STATUS_COUNT,        ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_BEHAVIOR_STATUS;

/** @brief 任务重要程度：从自由活动、固定承诺到紧急事务依次提高；优先级高也要满足执行及安全交接条件。 */
typedef enum {
    DOMINO_TASK_PRIORITY_FREE = 0,   ///< 可自由选择的安排，例如临时想去逛街或看电影。
    DOMINO_TASK_PRIORITY_COMMITTED,  ///< 已经承诺或有固定要求的安排，例如上班、上学或预约。
    DOMINO_TASK_PRIORITY_EMERGENCY,  ///< 涉及生存或安全的紧急事务，例如紧急就医或避险。
    DOMINO_TASK_PRIORITY_COUNT,      ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_PRIORITY;

/** @brief 任务结束的结果或原因：status 表示成功、失败还是取消，result 进一步说明为什么这样结束。 */
typedef enum {
    DOMINO_TASK_RESULT_NONE = 0,        ///< 任务尚未结束，还没有最终结果。
    DOMINO_TASK_RESULT_COMPLETED,       ///< 目标已经完成，对应任务成功。
    DOMINO_TASK_RESULT_INVALID_TARGET,  ///< 目标已失效而失败，例如目的地已不存在或不再可用。
    DOMINO_TASK_RESULT_INTERRUPTED,     ///< 被中断并最终结束，可对应失败或取消；临时挂起不使用此结果。
    DOMINO_TASK_RESULT_CANCELLED,       ///< 因取消安排而结束，对应任务取消。
    DOMINO_TASK_RESULT_COUNT,           ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_RESULT;

/** @brief 请求停止当前任务的原因，保存在 pending_stop_reason；这是待处理意图，不代表任务已经停止。 */
typedef enum {
    DOMINO_TASK_STOP_NONE = 0,  ///< 没有待处理的停止请求。
    DOMINO_TASK_STOP_PREEMPT,   ///< 请求先停下当前任务，交给 pending_child_task_id 指向的子任务处理。
    DOMINO_TASK_STOP_CANCEL,    ///< 请求取消任务，具体影响哪些任务由 cancel_scope 和 cancel_target_task_id 指定。
    DOMINO_TASK_STOP_DANGER,    ///< 因危险请求停止当前活动，后续可安排避险等任务。
    DOMINO_TASK_STOP_DEATH,     ///< 因人物死亡请求终结其全部任务，不再恢复挂起任务。
    DOMINO_TASK_STOP_COUNT,     ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_STOP_REASON;

/** @brief 取消操作影响多大范围；单任务和任务链通过 cancel_target_task_id 指定目标。 */
typedef enum {
    DOMINO_TASK_CANCEL_NONE = 0,  ///< 没有取消操作，例如只是临时让子任务先执行。
    DOMINO_TASK_CANCEL_TASK,      ///< 只取消当前任务，cancel_target_task_id 指向自身；父任务仍可判断是否恢复。
    DOMINO_TASK_CANCEL_CHAIN,     ///< 目标必须为父级或更高祖先；取消本任务至该祖先的同通道任务链，不包含祖先上方任务或其他通道。
    DOMINO_TASK_CANCEL_ALL,       ///< 取消该人物的全部任务，包括三个通道、挂起链和未启动任务；无需指定单个目标。
    DOMINO_TASK_CANCEL_COUNT,     ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_CANCEL_SCOPE;

/** @brief 行为正在等什么；仅 WAITING 状态使用非 NONE 原因，当前只定义了带明确等待时刻的情况。 */
typedef enum {
    DOMINO_BEHAVIOR_WAIT_NONE = 0,  ///< 当前没有等待，wait_until_ms 不生效。
    DOMINO_BEHAVIOR_WAIT_UNTIL,     ///< 等到 wait_until_ms 再检查；它是累计未冻结现实毫秒的绝对时刻，不是等待时长。
    DOMINO_BEHAVIOR_WAIT_COUNT,     ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_BEHAVIOR_WAIT_REASON;

/** @brief 行为专有参数，按 DominoBehavior.type 选用；随具体业务扩充。 */
typedef union DominoBehaviorParameters {
    struct {
        domino_runtime_ms_t target_active_ms;
    } sleep;
} DominoBehaviorParameters;

/** @brief 记录一个行为实际执行到哪里，供继续执行和保存后恢复使用。
 * @note 时刻均为累计未冻结的现实毫秒，0 是合法时刻；字段是否有效要看状态和标志，不能只看数值是否为 0。
 * @note 例如 1000 ms 开始、2000 ms 暂停、5000 ms 恢复并在 6000 ms 结算：
 * accounted_at_ms 为 6000，而 active_elapsed_ms 只有 2000，因为暂停期间不算有效进度。
 */
typedef struct DominoBehaviorProgress {
    domino_runtime_ms_t started_at_ms;      ///< 第一次真正开始的时刻；started_flag 为 true 时有效，暂停后恢复不重置。
    domino_runtime_ms_t ended_at_ms;        ///< 完成、跳过、失败或取消的时刻；仅在终态有效，暂停不算结束。
    domino_runtime_ms_t accounted_at_ms;    ///< 进度和效果已经算到哪个时刻；后续只结算此后的有效区间，避免重复计算。
    domino_runtime_ms_t active_elapsed_ms;  ///< 累计实际有效执行了多久，单位毫秒；不含冻结、暂停和无收益等待。
    domino_runtime_ms_t wait_until_ms;      ///< 等到哪个时刻再检查；仅在 wait_reason 非 NONE 时有效，不是等待时长。
    domino_status_t status;                 ///< 这一步当前的状态，取 DOMINO_BEHAVIOR_STATUS，例如正在执行、等待或已经完成。
    uint8_t wait_reason;                    ///< 正在等什么，取 DOMINO_BEHAVIOR_WAIT_REASON；非 WAITING 状态必须为 NONE。
    bool started_flag;                      ///< 是否曾真正启动；未开始就跳过或取消时为 false，不能靠开始时刻是否为 0 判断。
    bool effect_committed_flag;             ///< 扣款、消耗食物等一次性效果是否已提交，用于避免重复执行；不表示行为已经完成。
} DominoBehaviorProgress;

/** @brief 任务中的一个具体步骤，包含要做什么、去哪里、计划何时做，以及实际进度。
 * @note 行为身份由所属任务 ID 与行为数组下标确定，不另建全局行为索引。
 * @note 计划时间使用累计未冻结的现实毫秒，区间为 [planned_begin_ms, planned_end_ms)，与实际进度分开记录。
 */
typedef struct DominoBehavior {
    DominoBehaviorProgress progress;          ///< 这一步的实际执行记录，例如是否开始、已经做了多久、是否正在等待。
    DominoBehaviorParameters parameters;      ///< 由 type 决定使用哪组专有参数；目前定义了睡眠所需的目标有效时长。
    domino_building_id_t target_building_id;  ///< 这一步要前往或使用的建筑 ID；0 表示未指定，睡眠不依赖此字段。
    domino_runtime_ms_t planned_begin_ms;     ///< 预计开始这一步的时刻，不代表已经实际开始。
    domino_runtime_ms_t planned_end_ms;       ///< 预计结束这一步的时刻；与计划开始之差是预计耗时，不是已完成时长。
    DOMINO_BEHAVIOR_TYPE type;                ///< 这一步具体做什么，取 DOMINO_BEHAVIOR_TYPE，例如移动、吃饭或睡觉。
} DominoBehavior;

/** @brief 任务独占连续行为块，拥有独立游标和停止意图；0 是合法时刻，启动与终结由标志及状态判断。
 * @note 结构体复制只复制行为指针，不复制行为内容；跨线程快照须另存行为副本，不能共享任务池中的行为块。
 * @note 计划与实际时刻均为累计未冻结的现实毫秒；计划区间为 [planned_begin_ms, planned_end_ms)，不等于实际执行区间。
 * @note 父子任务属于同一人物和通道；直接子任务也可能挂起并交给下一层子任务，只有最深层活动任务占用通道。
 * @note 执行器接入后，步骤切换等操作须先检查版本上限，再递增 execution_version，使本任务的旧异步结果失效。
 */
typedef struct DominoTask {
    DominoBehavior* behaviors_ptr;           ///< 本任务独占的行为数组，共 behavior_count 项；store 接管后负责释放，存档保存内容而非地址。
    uint64_t execution_version;              ///< 本任务的执行版本，用于识别过期结果；新建或加载时置 1，不写入存档。
    domino_task_id_t task_id;                ///< 用来查找本任务的编号；新建草案填 0，提交后获得非零 ID。
    domino_human_id_t owner_human_id;        ///< 这项任务属于哪个人物，也就是由谁执行。
    domino_task_id_t parent_task_id;         ///< 本任务接替了哪个挂起的父任务；0 表示尚未挂在父任务下，结束后按此寻找可恢复的父任务。
    domino_task_id_t active_child_task_id;   ///< 已接管本任务的直接子任务 ID；非零时本任务为 SUSPENDED，0 表示没有。
    domino_task_id_t pending_child_task_id;  ///< 已提交但尚未启动的待交接任务 ID；安全停止后才可切换，0 表示没有。
    domino_task_id_t cancel_target_task_id;  ///< 取消目标 ID：TASK 指向自身，CHAIN 指向父级或更高祖先；无取消或取消全部时为 0。
    domino_runtime_ms_t planned_begin_ms;    ///< 整项任务预计开始的时刻，不代表已经实际启动。
    domino_runtime_ms_t planned_end_ms;      ///< 整项任务预计结束的时刻；挂起或调整安排后可能重新估算。
    domino_runtime_ms_t started_at_ms;       ///< 第一次真正启动的时刻；started_flag 为 true 时有效，挂起后恢复不重置。
    domino_runtime_ms_t ended_at_ms;         ///< 成功、失败或取消的时刻；仅在任务终态有效，挂起不写结束时间。
    DOMINO_TASK_TYPE type;                   ///< 整件事的目标类别，取 DOMINO_TASK_TYPE，例如上班、就医或睡眠。
    uint8_t channel_index;                   ///< 规划选定的通道下标：0 为主通道，1、2 为次通道；随存档保存，父子任务必须相同。
    uint8_t behavior_count;                  ///< 行为数组的有效项数，范围为 1～DOMINO_TASK_BEHAVIOR_COUNT_MAX。
    uint8_t current_behavior_index;          ///< 当前处理步骤的数组下标，从 0 开始；成功结束时等于 behavior_count，此值不能用于数组取值。
    domino_status_t status;                  ///< 整项任务的当前状态，取 DOMINO_TASK_STATUS，例如已计划、执行中或挂起。
    uint8_t result;                          ///< 任务最终如何结束，取 DOMINO_TASK_RESULT；尚未结束时为 NONE。
    uint8_t priority_class;                  ///< 任务有多重要，取 DOMINO_TASK_PRIORITY，区分自由活动、固定承诺和紧急事务。
    uint8_t pending_stop_reason;             ///< 为什么请求停止，取 DOMINO_TASK_STOP_REASON；NONE 表示无请求，有请求不代表已经停下。
    uint8_t cancel_scope;                    ///< 取消要影响哪些任务，取 DOMINO_TASK_CANCEL_SCOPE；与 cancel_target_task_id 一起说明取消范围。
    bool started_flag;                       ///< 是否曾真正启动；未开始就取消时为 false，开始时刻为 0 也可以是已启动。
} DominoTask;

#endif
