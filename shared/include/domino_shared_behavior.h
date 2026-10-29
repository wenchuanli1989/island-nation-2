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
    DOMINO_TASK_TYPE_TRAVEL,         ///< 旅游：安排出行、参观等活动，可包含过夜等行为。
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
    DOMINO_TASK_STATUS_RUNNING,      ///< 已开始并占用活动通道；当前行为可以正在执行或等待。
    DOMINO_TASK_STATUS_SUCCEEDED,    ///< 任务成功结束，行为已完成或按规则跳过。
    DOMINO_TASK_STATUS_FAILED,       ///< 本次任务执行无法完成，已经以失败结束，所属人物进入异常状态。
    DOMINO_TASK_STATUS_CANCELLED,    ///< 任务取消的结果状态，所属人物进入异常；不表示提供取消操作接口。
    DOMINO_TASK_STATUS_COUNT,        ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_STATUS;

/** @brief 行为状态：当前这个步骤进行到哪一步；DONE、SKIPPED、FAILED、CANCELLED 都是终态。 */
typedef enum {
    DOMINO_BEHAVIOR_STATUS_PENDING = 0,  ///< 还没轮到或尚未激活这个步骤。
    DOMINO_BEHAVIOR_STATUS_ACTIVE,       ///< 正在执行，并累计该行为的有效进度。
    DOMINO_BEHAVIOR_STATUS_WAITING,      ///< 已启动，但正在等条件或时刻；只计算等待规则允许的成本，不算正常执行进度。
    DOMINO_BEHAVIOR_STATUS_DONE,         ///< 这个步骤已经正常完成。
    DOMINO_BEHAVIOR_STATUS_SKIPPED,      ///< 尚未执行就按规则跳过，例如省略不需要的可选晚餐。
    DOMINO_BEHAVIOR_STATUS_FAILED,       ///< 本次行为执行无法完成，已经以失败结束。
    DOMINO_BEHAVIOR_STATUS_CANCELLED,    ///< 行为的执行安排已被撤销；可能尚未开始，也可能已执行一部分。
    DOMINO_BEHAVIOR_STATUS_COUNT,        ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_BEHAVIOR_STATUS;

/** @brief 任务重要程度：从自由活动、固定承诺到紧急事务依次提高；优先级高也要满足执行条件。 */
typedef enum {
    DOMINO_TASK_PRIORITY_FREE = 0,   ///< 可自由选择的安排，例如临时想去逛街或看电影。
    DOMINO_TASK_PRIORITY_COMMITTED,  ///< 已经承诺或有固定要求的安排，例如上班、上学或预约。
    DOMINO_TASK_PRIORITY_EMERGENCY,  ///< 涉及生存或安全的紧急事务，例如紧急就医或避险。
    DOMINO_TASK_PRIORITY_COUNT,      ///< 枚举取值上界，仅用于计数和范围检查。
} DOMINO_TASK_PRIORITY;

/** @brief 任务中的一个具体步骤，包含要做什么、去哪里、计划何时做，以及实际进度。
 * @note 行为身份由所属任务 ID 与行为数组下标确定，不另建全局行为索引。
 * @note 计划时间使用累计未冻结的现实毫秒，区间为 [planned_begin_ms, planned_end_ms)，与实际进度分开记录。
 * @note 当前睡眠的目标有效时长由 planned_end_ms - planned_begin_ms 推导；首次启动时修订区间，执行期间保持区间长度不变。
 * @note 实际时刻同样使用累计未冻结的现实毫秒；0 是合法时刻，字段是否有效由状态和标志决定。
 */
typedef struct DominoBehavior {
    domino_building_id_t target_building_id;  ///< 这一步要前往或使用的建筑 ID；0 表示未指定，睡眠不依赖此字段。
    domino_runtime_ms_t planned_begin_ms;     ///< 预计开始这一步的时刻，不代表已经实际开始。
    domino_runtime_ms_t planned_end_ms;       ///< 预计结束这一步的时刻；与计划开始之差是预计耗时，不是已完成时长。
    domino_runtime_ms_t started_at_ms;        ///< 第一次真正开始的时刻；started_flag 为 true 时有效，加载后继续执行不重置。
    domino_runtime_ms_t ended_at_ms;          ///< 完成、跳过、失败或取消的时刻；仅在终态有效。
    domino_runtime_ms_t accounted_at_ms;      ///< 进度和效果已经算到哪个时刻；后续只结算此后的有效区间，避免重复计算。
    domino_runtime_ms_t active_elapsed_ms;    ///< 累计实际有效执行了多久，单位毫秒；不含冻结和无收益等待。
    domino_runtime_ms_t wait_until_ms;        ///< 等到哪个绝对时刻再检查；仅在 WAITING 状态下有效，0 也是合法时刻，不是等待时长。
    DOMINO_BEHAVIOR_TYPE type;                ///< 这一步具体做什么，取 DOMINO_BEHAVIOR_TYPE，例如移动、吃饭或睡觉。
    domino_status_t status;                   ///< 这一步当前的状态，取 DOMINO_BEHAVIOR_STATUS，例如正在执行、等待或已经完成。
    bool started_flag;                        ///< 是否曾真正启动；未开始就跳过或取消时为 false，不能靠开始时刻是否为 0 判断。
    bool effect_committed_flag;               ///< 扣款、消耗食物等一次性效果是否已提交，用于避免重复执行；不表示行为已经完成。
} DominoBehavior;

/** @brief 任务独占连续行为块，拥有独立游标；0 是合法时刻，启动与终结由标志及状态判断。
 * @note 结构体复制只复制行为指针，不复制行为内容；跨线程快照须另存行为副本，不能共享任务池中的行为块。
 * @note 计划与实际时刻均为累计未冻结的现实毫秒；计划区间为 [planned_begin_ms, planned_end_ms)，不等于实际执行区间。
 * @note 步骤切换和任务终结须先检查版本上限，再递增 execution_version，使本任务的旧异步结果失效。
 */
typedef struct DominoTask {
    DominoBehavior* behaviors_ptr;         ///< 本任务独占的行为数组，共 behavior_count 项；store 接管后负责释放，存档保存内容而非地址。
    uint64_t execution_version;            ///< 本任务的执行版本，用于识别过期结果；新建或加载时置 1，不写入存档。
    domino_task_id_t task_id;              ///< 用来查找本任务的编号；新建草案填 0，提交后获得非零 ID。
    domino_human_id_t owner_human_id;      ///< 这项任务属于哪个人物，也就是由谁执行。
    domino_runtime_ms_t planned_begin_ms;  ///< 整项任务预计开始的时刻，不代表已经实际启动。
    domino_runtime_ms_t planned_end_ms;    ///< 整项任务预计结束的时刻；调整安排后可能重新估算。
    domino_runtime_ms_t started_at_ms;     ///< 第一次真正启动的时刻；started_flag 为 true 时有效，加载后继续执行不重置。
    domino_runtime_ms_t ended_at_ms;       ///< 成功、失败或取消的时刻；仅在任务终态有效。
    DOMINO_TASK_TYPE type;                 ///< 整件事的目标类别，取 DOMINO_TASK_TYPE，例如上班、就医或睡眠。
    uint8_t channel_index;                 ///< 规划选定的通道下标：0 为主通道，1、2 为次通道；随存档保存。
    uint8_t behavior_count;                ///< 行为数组的有效项数，范围为 1～DOMINO_TASK_BEHAVIOR_COUNT_MAX。
    uint8_t current_behavior_index;        ///< 当前处理步骤的数组下标，从 0 开始；成功结束时等于 behavior_count，此值不能用于数组取值。
    domino_status_t status;                ///< 整项任务的当前状态及结束结果，取 DOMINO_TASK_STATUS。
    uint8_t priority_class;                ///< 任务有多重要，取 DOMINO_TASK_PRIORITY，区分自由活动、固定承诺和紧急事务。
    bool started_flag;                     ///< 是否曾真正启动；未开始就取消时为 false，开始时刻为 0 也可以是已启动。
} DominoTask;

#endif
