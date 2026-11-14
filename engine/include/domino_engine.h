#ifndef DOMINO_ENGINE_H
#define DOMINO_ENGINE_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"

/** @file domino_engine.h
 * @brief 引擎对外接口；客户端不得直接提交任务请求、规划请求或规划结果。
 * @note 任务由引擎内部自主决策和规划，不为测试增加对外任务提交接口。
 */

#include "domino_shared_human_task.h"

typedef enum DominoEngineMsgType {
    DOMINO_ENGINE_MSG_NONE = 0,
    DOMINO_ENGINE_MSG_QUERY_NPC,
    DOMINO_ENGINE_MSG_SET_NPC_NAME,
    DOMINO_ENGINE_MSG_NPC_RESULT,
} DominoEngineMsgType;
typedef enum {
    DOMINO_NPC_QUERY_COMPLETE,
    DOMINO_NPC_NAME_CHANGED,
    DOMINO_NPC_UNCHANGED,
    DOMINO_NPC_VERSION_CONFLICT,
    DOMINO_NPC_REJECTED
} DominoNpcResultStatus;
typedef struct DominoNpcRequest {
    uint64_t session_id;
    uint64_t request_id;
    uint64_t expected_name_version;
    domino_human_id_t human_id;
    uint16_t name_len;
    char name[DOMINO_HUMAN_DISPLAY_NAME_MAX + 1];
} DominoNpcRequest;
typedef struct DominoNpcChannelSummary {
    domino_task_id_t task_id;
    domino_behavior_id_t behavior_id;
    DOMINO_TASK_TYPE task_type;
    DOMINO_BEHAVIOR_TYPE behavior_type;
    domino_runtime_ms_t active_elapsed_ms;
    uint8_t behavior_status;
} DominoNpcChannelSummary;
/** @brief 单次读锁内复制的有界摘要，无借用指针；读取不补结算。 */
typedef struct DominoNpcSummary {
    uint64_t name_version;
    uint64_t implementation_count;
    DominoNpcChannelSummary channels[DOMINO_HUMAN_TASK_CHANNEL_COUNT];
    domino_runtime_ms_t read_at_ms;
    domino_runtime_ms_t settled_at_ms;
    domino_human_id_t human_id;
    uint8_t energy, food, water, hygiene, recreation, social;
    uint8_t motivation_count;
    uint8_t status;
    char name[DOMINO_HUMAN_DISPLAY_NAME_MAX + 1];
} DominoNpcSummary;
typedef struct DominoNpcResult {
    uint64_t session_id;
    uint64_t request_id;
    DominoNpcSummary summary;
    DominoNpcResultStatus status;
    DOMINO_CODE reason;
} DominoNpcResult;
/** @brief 外部消息按 id → payload → type 布局；请求关联身份不被队列 ID 覆盖。 */
typedef struct DominoEngineMessage {
    uint64_t id;
    union {
        DominoNpcRequest npc;
        DominoNpcResult result;
    } payload;
    DominoEngineMsgType type;
} DominoEngineMessage;
static_assert(offsetof(DominoEngineMessage, id) == 0, "queue ID first");
static_assert(sizeof(DominoEngineMessage) <= 1200, "bounded message fits MTU budget");

typedef struct DominoPlannerConfig {
    uint32_t high_quota;
    domino_runtime_ms_t request_timeout_ms;
    uint32_t computation_budget;
} DominoPlannerConfig;
typedef struct DominoEngineLaunchConfig {
    /**
     * @brief 必填存档目录；初始化时复制路径并移除末尾 '/'，不接受根目录或末级目录名为 .、.. 的路径。
     * 仅根 meta.json 不存在时创建空世界；无法加载有效世界时打印致命错误并退出进程。
     */
    const char* storage_path;
    uint64_t client_id;               ///< 接入层绑定的管理身份，0 使用本地身份 1；请求不能指定身份。
    DominoPlannerConfig planners[3];  ///< 每类全零时使用默认值 4/3000ms/512；其他配置要求全部为正。

    /**
     * @brief 下次保存是否记录分片和模块 meta 的 CRC32C/size；关闭时摘要写 0。
     * 加载始终使用存档自身声明的校验策略。
     */
    bool storage_integrity_verify;

} DominoEngineLaunchConfig;

/**
 * @brief 准备固定运行资源并加载存档，保持时钟冻结，不创建线程；成功调用的线程登记为客户端线程。
 * @note 日志文件、主循环及 human/game_timer 队列、映射表和同步对象在本阶段初始化；业务容器仍按数据量增长。
 * 世界加载及引用绑定后，完成 Nav 邻接和 human 首次唤醒的重建；返回时运行所需的初始数据已就绪，不推进任务。
 * @note 首次使用依赖静态初值，再次使用须先完成 Destroy；Init 不重复清空已复位的模块状态。
 * @note 生命周期调用须串行，不能与其他引擎操作并发；非法生命周期调用及不可恢复错误直接打印 stderr 并 exit(EXIT_FAILURE)。
 * Init 仅接受未初始化，Start 仅接受已初始化，Stop 接受已初始化或运行中，Destroy 仅接受已停止；重复或错序不作无操作处理。
 * 运行阶段的非致命错误与正常日志进入日志模块，按配置写文件或 stdout；初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断直接输出 stdout。
 * 标准 exit 会执行已注册退出处理；致命错误可能发生在持锁期间，退出处理不可重新调用引擎接口。
 */
extern void dominoEngineInit(DominoEngineLaunchConfig launch_config);

typedef struct DominoEngineClientData {
    uint64_t session_id;         ///< 本轮会话身份，旧会话请求会被拒绝。
    uint64_t game_date_time_ns;  ///< Start 写入的累计未冻结现实纳秒基准，不会持续更新；业务计时使用 32 位现实毫秒。
    /* 以下日历字段尚未由引擎计算或写入。 */
    uint32_t game_date_time_sec;
    uint32_t game_time;
    uint32_t game_year;
    uint32_t game_month;

    uint8_t reserved[4064];  ///< 预留字段，填充至 4096 字节。
} DominoEngineClientData;
static_assert(sizeof(DominoEngineClientData) == 4096U, "client data must be 4096 bytes");

/**
 * @brief 客户端使用已初始化的资源，在冻结状态下依次启动 logger、human 执行／规划线程、game_timer 和主循环。
 * @note 所有线程创建完成后进入 RUNNING，解冻并唤醒业务线程；本阶段不再创建固定队列、映射表或同步对象。
 * @param client_data_out_ptr 可为 nullptr；非空时仅写入 game_date_time_ns。
 * @note 调度已由 Init 准备；返回只表示线程启动完成，不表示业务规划或任务已执行；启动失败直接打印致命错误并退出进程。
 */
extern void dominoEngineStart(DominoEngineClientData* client_data_out_ptr);

/**
 * @brief 客户端进入 STOPPING，停止已启动的线程并排空日志，保留资源供 Destroy 保存和释放。
 * @pre 引擎处于 INITIALIZED 或 RUNNING，且由 Init 登记的客户端线程串行调用；已冻结的运行实例同样可以停止。
 * @note 运行过的实例先停钟并唤醒业务线程，依次 join 主循环、执行／规划线程及 game_timer，通过 human 模块入口统一结算全部人物，
 * 包含异常人物遗留的活动通道和自然需求，再排空日志并 join writer。仅 Init、未 Start 时跳过业务线程 join 和模块结算，logger 关闭入队并刷新输出；
 * 保持已加载及配置的事实。
 * 任务业务失败记录终态并继续其他结算；停止或结算中的基础设施及不可恢复错误直接终止进程，不保存。
 * 成功后进入 STOPPED，世界、队列、同步对象和日志文件均保留，全部线程已退出，尚未保存；随后显式调用 Destroy，不支持直接 Start 或 Thaw。
 */
extern void dominoEngineStop(void);

/**
 * @brief 释放已停止的 game_timer 和主循环队列，保存世界，再释放 human 等世界模块和日志，完成后回到未初始化状态。
 * @pre 引擎处于 STOPPED，且由 Init 登记的客户端线程串行调用；无论是否 Start，都必须先由外部完成 Stop。
 * @note 不停钟、不停止或 join 任何线程；未完成 Stop 直接调用属于生命周期错误，打印 stderr 并 exit(EXIT_FAILURE)。
 * Nav 的 DestroyBefore 只规整存档布局，不再推进或结算业务。内部队列不落盘，本阶段保存的 human 事实供下次 Init 重建工作。
 * 日志资源最后销毁；保存及销毁阶段的非致命诊断通过 DOMINO_PRINT_MESSAGE 直接输出 stdout，不调用日志记录接口。
 * 返回前容器、索引指针、game_timer 和引擎实例状态已复位；time 在保存后恢复为基准、采样起点、版本均为零的冻结快照。
 * 内存时钟清空不修改存档累计时间，下一次 Init 从存档恢复；不依赖下一次 Init 清理旧实例状态。
 * 所有阶段的致命错误均使用 DOMINO_EXIT_WITH_ERROR 直接输出 stderr 并退出。
 * 须排除其他 API 并发；保存或释放失败直接终止进程，成功后允许重新 Init。
 */
extern void dominoEngineDestroy(void);

/**
 * @brief 客户端冻结业务时钟，不通知或等待业务线程；重复冻结保持原时刻。
 * @note 业务线程在批次入口观察冻结后不开始新批次，消息及规划结果留队，解冻后继续消费。
 * 若冻结发生在批次检查之后，该批仍可继续出队、处理和入队；返回不表示世界已停止写入或可立即保存。
 * 队列自身不检查冻结，引擎内部当前批次的规划请求及结果仍可入队，日志线程继续写出。
 * 只允许客户端在 RUNNING 调用；非法调用或时钟故障直接打印致命错误并退出进程。
 */
extern void dominoEngineFreezeTime(void);

/**
 * @brief 客户端校验业务时钟范围、解冻并唤醒主循环及业务线程，不等待恢复回执或重建已有唤醒。
 * @note 只允许客户端在 RUNNING 且时钟冻结时调用；非法调用、时钟或通知故障直接打印致命错误并退出进程。
 */
extern void dominoEngineThawTime(void);

/**
 * @brief 客户端向引擎主循环发送消息。
 * @note 不提供客户端任务请求、规划请求或规划结果消息，不能通过本入口提交这些内容。
 * @param message_ptr 可写消息；成功后 id 为本次入队生成的 ID。
 * @note 只允许登记的客户端线程在 RUNNING 期间发送，冻结不影响入队；主循环观察冻结后保留消息，解冻后继续处理。
 * 若冻结发生在主循环的批次检查之后，该批仍可继续出队和处理。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；满载、扩容失败、ID 耗尽等传输故障打印致命错误并退出进程，不等待空位。
 *         请求状态或线程不合法时记录非致命错误并返回；入队仍需获取队列 mutex。
 */
extern DOMINO_CODE dominoEnginePushMsg(DominoEngineMessage* message_ptr);

/**
 * @brief 客户端接收引擎消息。
 * @pre 只允许登记的客户端线程在 RUNNING 期间接收。
 * @param blocking 正常状态下无消息时，true 等待消息，false 返回 `ERR_QUEUE_EMPTY`；两种模式均需获取队列 mutex。
 * @note 冻结不影响取出已有消息；暂停时空队列的阻塞调用仍等待生产者，需继续控制游戏时使用非阻塞模式。
 *       系统或同步故障直接打印致命错误并退出进程。
 * @return `CODE_OK` 成功；访问资格合法时空指针返回 `ERR_NULL_POINTER`；引擎未初始化返回 `ERR_NOT_INITIALIZED`；
 *         其他生命周期或线程不合法时返回 `ERR_GAME_STATE_INVALID`，以上拒绝均记录一次非致命诊断。
 */
extern DOMINO_CODE dominoEnginePopMsg(DominoEngineMessage* message_out_ptr, bool blocking);

#ifdef DOMINO_PLAYGROUND_MODE
extern int dominoEnginePlayground(int argc, char* argv[]);
#endif
#endif
