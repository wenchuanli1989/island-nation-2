#ifndef DOMINO_ENGINE_H
#define DOMINO_ENGINE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

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
     * 仅根 meta.json 不存在时创建空世界；查询、读取或解析失败均返回错误。
     */
    const char* storage_path;

    /**
     * @brief 下次保存是否记录分片和模块 meta 的 CRC32C/size；关闭时摘要写 0。
     * 加载始终使用存档自身声明的校验策略。
     */
    bool storage_integrity_verify;
} DominoEngineLaunchConfig;

/**
 * @brief 初始化引擎并加载存档，保持时钟冻结；成功调用的线程登记为客户端线程。
 * @return CODE_OK 成功；失败返回首个负错误码，重复初始化为 ERR_ALREADY_INITIALIZED。
 * @note 失败立即终结本次引擎生命周期，不回滚、不清理、不重试，也不主动结束进程。
 * 宿主报告错误并结束进程；在此之前须保活残留线程可能访问的资源，不得再次调用 Init/Run/Exit 或业务接口。
 * 生命周期调用须串行，不能与其他引擎操作并发。
 * lib 中库函数保留既有终止行为，允许内部 abort；该情况不会转换为返回码或生命周期首错。
 */
extern DOMINO_CODE dominoEngineInit(DominoEngineLaunchConfig launch_config);

/** @brief 读取本次实例首个生命周期或运行期错误；尚未失败返回 CODE_OK，不触发清理或状态变更。
 * @note 生命周期失败后这是唯一允许调用的引擎接口，宿主须结束进程；此前须保活残留线程可访问的资源。
 * 队列满、运行期传输故障、冻结和解冻失败同样记录在此；错误通知不依赖业务队列空位。
 */
extern DOMINO_CODE dominoEngineGetLifecycleError(void);

typedef struct DominoEngineClientData {
    uint64_t game_date_time_ns;  ///< Run 写入的累计未冻结现实纳秒基准，不会持续更新；业务计时使用 32 位现实毫秒。
    /* 以下日历字段尚未由引擎计算或写入。 */
    uint32_t game_date_time_sec;
    uint32_t game_time;
    uint32_t game_year;
    uint32_t game_month;

    uint8_t reserved[4072];  ///< 预留字段，填充至 4096 字节。
} DominoEngineClientData;
static_assert(sizeof(DominoEngineClientData) == 4096U, "client data must be 4096 bytes");

/**
 * @brief 客户端在冻结状态下依次启动 human、timer 和主循环，随后进入 RUNNING，直接解冻并唤醒各线程。
 * @param client_data_out_ptr 可为 nullptr；成功时非空输出仅写入 game_date_time_ns。
 * @return CODE_OK 成功；状态、启动校验或资源步骤失败终结生命周期，保留资源并返回首错，不再清理或重试。
 * @note 启动、冻结、恢复及退出均由客户端串行协调，主循环只处理自身消息。
 * 已加载活动睡眠须在 Run 前显式授权，不从建筑引用推断许可。
 * 首次解冻失败同样终结实例，不允许再次 Run/Thaw/Exit。仅成功时写入输出。
 * 已授权空闲人物随后按精力阈值自主规划睡眠；返回成功不表示所有人物已完成规划或启动。
 */
extern DOMINO_CODE dominoEngineRun(DominoEngineClientData* client_data_out_ptr);

/**
 * @brief 客户端直接发布退出标志、停钟并唤醒全部业务线程，等待当前批次结束后统一结算、保存并释放资源。
 * 依次 join 主循环、执行／规划线程及 timer，全部结束前保活其投递目标；任何停止或结算失败均不保存。
 * Init 后未 Run 可直接保存已加载事实；日志在保存及世界释放后停止。
 * @return 保存、释放成功返回 CODE_OK；停止、保存或释放步骤失败立即终结生命周期，保留尚未释放资源并返回首错。
 * @note 须排除其他 API 并发。退出资源步骤失败不回滚、不继续清理、不允许再次 Exit，由宿主报告错误并结束进程。
 * 成功退出后允许同进程重新 Init；任何失败均记录生命周期错误，不保存不完整快照。
 */
extern DOMINO_CODE dominoEngineExit(void);

/**
 * @brief 客户端直接冻结业务时钟，不通知或等待业务线程；重复冻结保持原时刻。
 * 已开始的有限批次完整执行，各业务线程在下一批次入口观察冻结；消息和在途规划留队，恢复后继续消费。
 * 暂停期间拒绝同步睡眠业务提交；消息队列仍可收发，日志线程继续正常写出。
 * @return CODE_OK 仅表示时钟已冻结，不保证世界已停止写入或可立即保存；失败记录首错并终结实例。
 * 非客户端线程或尚未进入 RUNNING 返回 ERR_GAME_STATE_INVALID，并终结本次实例。
 */
extern DOMINO_CODE dominoEngineFreezeTime(void);

/**
 * @brief 客户端直接校验业务时钟范围、解冻并唤醒主循环及业务线程；不等待恢复回执，不扫描人物或重建已有唤醒。
 * @return 成功恢复返回 CODE_OK；非客户端线程、尚未 Run 或未暂停返回 ERR_GAME_STATE_INVALID。
 * 时钟校验、解冻或通知失败均记录首错并终结实例，不允许再次恢复。
 */
extern DOMINO_CODE dominoEngineThawTime(void);

/**
 * @brief 客户端显式确认人物在指定当前建筑持续获准睡眠，并允许后续自主睡眠；许可仅保留在本次初始化的运行态中。
 * @note Init 后尚未 Run 时直接配置，运行时交执行线程同步处理；暂停期间拒绝提交。成功表示许可成立，不承诺已创建自主任务。
 * 自主睡眠要求三槽为空、无任何非终态任务且有容量，按精力阈值选择主通道；不寻找地点或重排日程。
 * 本批不支持运行期间撤权或搬离。
 * 加载 ACTIVE 睡眠须在 Run 前重新确认许可；此接口不能补授已过去时段的许可。
 */
extern DOMINO_CODE dominoEngineAuthorizeSleep(domino_human_id_t human_id, domino_building_id_t building_id);

/**
 * @brief 客户端确认睡眠许可并提交单行为睡眠，成功时输出任务 ID 并安排执行；失败保持输出不变。
 * @note Init 后尚未 Run 时仅提交 PLANNED，运行中由执行线程校验、提交并随后启动；暂停期间拒绝提交。成功同时保留后续自主睡眠许可。
 * 后续自主安排使用主通道，须满足完全空闲、无非终态任务及容量条件；不自动寻找地点。
 * 只支持同步睡眠内核的单活动形态，三个通道仍可任选。调用须与其他客户端生命周期和睡眠 API 串行。
 */
extern DOMINO_CODE dominoEngineSubmitSleep(domino_human_id_t human_id, domino_building_id_t building_id, uint8_t channel_index,
                                           domino_task_id_t* task_id_out_ptr);

/**
 * @brief 客户端向引擎主循环发送消息。
 * @param message_ptr 可写消息；成功后 id 为本次入队生成的 ID。
 * @note 只允许登记的客户端线程在 RUNNING 期间发送，冻结不影响入队；业务消费者在解冻后继续处理。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；满载、扩容失败、ID 耗尽等队列错误终结实例，不等待空位。
 *         入队仍需获取队列 mutex；失败首错可通过 GetLifecycleError 读取。
 */
extern DOMINO_CODE dominoEnginePushMsg(DominoEngineMessage* message_ptr);

/**
 * @brief 客户端接收引擎消息。
 * @param blocking 无消息时，true 等待消息，false 返回 `ERR_QUEUE_EMPTY`；两种模式均需获取队列 mutex。
 * @note 冻结不影响取出已有消息；暂停时空队列的阻塞调用仍等待生产者，需继续控制游戏时使用非阻塞模式。
 *       实例失败会唤醒等待者并返回首错。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；引擎队列未初始化返回 `ERR_NOT_INITIALIZED`；
 *         消费端已关停返回 `ERR_QUEUE_CONSUMER_STOPPED`；生产端已关停且队列已空返回 `ERR_QUEUE_PRODUCER_STOPPED`。
 */
extern DOMINO_CODE dominoEnginePopMsg(DominoEngineMessage* message_out_ptr, bool blocking);

#ifdef DOMINO_PLAYGROUND_MODE
extern int dominoEnginePlayground(int argc, char* argv[]);
#endif
#endif
