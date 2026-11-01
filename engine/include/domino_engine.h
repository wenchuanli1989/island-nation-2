#ifndef DOMINO_ENGINE_H
#define DOMINO_ENGINE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"

/** @file domino_engine.h
 * @brief 引擎对外接口；客户端不得直接提交任务请求、规划请求或规划结果。
 * @note 任务由引擎内部自主决策和规划，不为测试增加对外任务提交接口。
 */

typedef enum DominoEngineMsgType {
    DOMINO_ENGINE_MSG_NONE = 0,
} DominoEngineMsgType;

typedef struct DominoEngineMessage {
    uint64_t id;  ///< 入队成功时由所属队列生成并回写的消息 ID。
    uint64_t u64[4];
    uint32_t u32[5];
    uint32_t type;
} DominoEngineMessage;

static_assert(sizeof(DominoEngineMessage) == 64U, "engine message must be 64 bytes");

typedef struct DominoEngineLaunchConfig {
    /**
     * @brief 必填存档目录；初始化时复制路径并移除末尾 '/'，不接受根目录或末级目录名为 .、.. 的路径。
     * 仅根 meta.json 不存在时创建空世界；无法加载有效世界时打印致命错误并退出进程。
     */
    const char* storage_path;

    /**
     * @brief 下次保存是否记录分片和模块 meta 的 CRC32C/size；关闭时摘要写 0。
     * 加载始终使用存档自身声明的校验策略。
     */
    bool storage_integrity_verify;
} DominoEngineLaunchConfig;

/**
 * @brief 准备固定运行资源并加载存档，保持时钟冻结，不创建线程；成功调用的线程登记为客户端线程。
 * @note 日志文件、主循环及 human/game_timer 队列、映射表和同步对象在本阶段初始化；业务容器仍按数据量增长。
 * @note 首次使用依赖静态初值，再次使用须先完成 Destroy；Init 不重复清空已复位的模块状态。
 * @note 生命周期调用须串行，不能与其他引擎操作并发；非法生命周期调用及不可恢复错误直接打印 stderr 并 exit(EXIT_FAILURE)。
 * Init 仅接受未初始化，Start 仅接受已初始化，Stop 接受已初始化或运行中，Destroy 仅接受已停止；重复或错序不作无操作处理。
 * 运行阶段的非致命错误与正常日志进入日志模块，按配置写文件或 stdout；初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断直接输出 stdout。
 * 标准 exit 会执行已注册退出处理；致命错误可能发生在持锁期间，退出处理不可重新调用引擎接口。
 */
extern void dominoEngineInit(DominoEngineLaunchConfig launch_config);

typedef struct DominoEngineClientData {
    uint64_t game_date_time_ns;  ///< Start 写入的累计未冻结现实纳秒基准，不会持续更新；业务计时使用 32 位现实毫秒。
    /* 以下日历字段尚未由引擎计算或写入。 */
    uint32_t game_date_time_sec;
    uint32_t game_time;
    uint32_t game_year;
    uint32_t game_month;

    uint8_t reserved[4072];  ///< 预留字段，填充至 4096 字节。
} DominoEngineClientData;
static_assert(sizeof(DominoEngineClientData) == 4096U, "client data must be 4096 bytes");

/**
 * @brief 客户端使用已初始化的资源，在冻结状态下依次启动 logger、human 执行／规划线程、game_timer 和主循环。
 * @note 所有线程创建完成后进入 RUNNING，解冻并唤醒业务线程；本阶段不再创建固定队列、映射表或同步对象。
 * @param client_data_out_ptr 可为 nullptr；非空时仅写入 game_date_time_ns。
 * @note 返回只表示启动完成，不表示所有人物已经恢复执行、完成规划或开始任务；启动失败直接打印致命错误并退出进程。
 */
extern void dominoEngineStart(DominoEngineClientData* client_data_out_ptr);

/**
 * @brief 客户端进入 STOPPING，停止已启动的线程并排空日志，保留资源供 Destroy 保存和释放。
 * @pre 引擎处于 INITIALIZED 或 RUNNING，且由 Init 登记的客户端线程串行调用；已冻结的运行实例同样可以停止。
 * @note 运行过的实例先停钟并唤醒业务线程，依次 join 主循环、执行／规划线程及 game_timer，统一结算正常人物，异常人物直接跳过，再排空日志并 join
 * writer。 仅 Init、未 Start 时各模块跳过线程 join，logger 关闭入队并刷新输出；保持已加载及配置的事实，不结算人物。
 * 停止或结算失败直接终止进程，不保存。
 * 成功后进入 STOPPED，世界、队列、同步对象和日志文件均保留，全部线程已退出，尚未保存；随后显式调用 Destroy，不支持直接 Start 或 Thaw。
 */
extern void dominoEngineStop(void);

/**
 * @brief 释放已停止的 game_timer 和主循环队列，保存世界，再释放 human 等世界模块和日志，完成后回到未初始化状态。
 * @pre 引擎处于 STOPPED，且由 Init 登记的客户端线程串行调用；无论是否 Start，都必须先由外部完成 Stop。
 * @note 不停钟、不停止或 join 任何线程；未完成 Stop 直接调用属于生命周期错误，打印 stderr 并 exit(EXIT_FAILURE)。
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
