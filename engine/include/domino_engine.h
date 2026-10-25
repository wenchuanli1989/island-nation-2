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
 * @note 日志文件、主循环及 human/timer 队列、映射表和同步对象在本阶段初始化；业务容器仍按数据量增长。
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
 * @brief 客户端使用已初始化的资源，在冻结状态下依次启动 logger、human 执行／规划线程、timer 和主循环。
 * @note 所有线程创建完成后进入 RUNNING，解冻并唤醒业务线程；本阶段不再创建固定队列、映射表或同步对象。
 * @param client_data_out_ptr 可为 nullptr；非空时仅写入 game_date_time_ns。
 * @note 已加载活动睡眠须在 Start 前显式授权，不从建筑引用推断许可。
 * 返回只表示启动完成，不表示所有人物已经完成规划或开始任务；启动失败直接打印致命错误并退出进程。
 */
extern void dominoEngineStart(DominoEngineClientData* client_data_out_ptr);

/**
 * @brief 客户端进入 STOPPING，停止已启动的线程并排空日志，保留资源供 Destroy 保存和释放。
 * @pre 引擎处于 INITIALIZED 或 RUNNING，且由 Init 登记的客户端线程串行调用；已冻结的运行实例同样可以停止。
 * @note 运行过的实例先停钟并唤醒业务线程，依次 join 主循环、执行／规划线程及 timer，统一结算人物，再排空日志并 join writer。
 * 仅 Init、未 Start 时各模块跳过线程 join，logger 关闭入队并刷新输出；保持已加载及配置的事实，不结算人物或要求睡眠授权。
 * 停止或结算失败直接终止进程，不保存。
 * 成功后进入 STOPPED，世界、队列、同步对象和日志文件均保留，全部线程已退出，尚未保存；随后显式调用 Destroy，不支持直接 Start 或 Thaw。
 */
extern void dominoEngineStop(void);

/**
 * @brief 释放已停止的 timer 和主循环队列，保存世界，再释放 human 等世界模块和日志，完成后回到未初始化状态。
 * @pre 引擎处于 STOPPED，且由 Init 登记的客户端线程串行调用；无论是否 Start，都必须先由外部完成 Stop。
 * @note 不停钟、不停止或 join 任何线程；未完成 Stop 直接调用属于生命周期错误，打印 stderr 并 exit(EXIT_FAILURE)。
 * 日志资源最后销毁；保存及销毁阶段的非致命诊断通过 DOMINO_PRINT_MESSAGE 直接输出 stdout，不调用日志记录接口。
 * 返回前容器、索引指针、timer 和引擎实例状态已复位；time 在保存后恢复为基准、采样起点、版本均为零的冻结快照。
 * 内存时钟清空不修改存档累计时间，下一次 Init 从存档恢复；不依赖下一次 Init 清理旧实例状态。
 * 所有阶段的致命错误均使用 DOMINO_EXIT_WITH_ERROR 直接输出 stderr 并退出。
 * 须排除其他 API 并发；保存或释放失败直接终止进程，成功后允许重新 Init。
 */
extern void dominoEngineDestroy(void);

/**
 * @brief 客户端冻结业务时钟，不通知或等待业务线程；重复冻结保持原时刻。
 * @note 已开始的有限批次完整执行，业务线程在下一批次入口观察冻结；消息和在途规划留队，恢复后继续消费。
 * 返回不表示世界已停止写入或可立即保存。冻结期间拒绝同步睡眠业务提交；消息队列仍可收发，日志线程继续写出。
 * 只允许客户端在 RUNNING 调用；非法调用或时钟故障直接打印致命错误并退出进程。
 */
extern void dominoEngineFreezeTime(void);

/**
 * @brief 客户端校验业务时钟范围、解冻并唤醒主循环及业务线程，不等待恢复回执或重建已有唤醒。
 * @note 只允许客户端在 RUNNING 且时钟冻结时调用；非法调用、时钟或通知故障直接打印致命错误并退出进程。
 */
extern void dominoEngineThawTime(void);

/**
 * @brief 客户端显式确认人物在指定当前建筑持续获准睡眠，并允许后续自主睡眠；许可仅保留在本次初始化的运行态中。
 * @note Init 后尚未 Start 时直接配置，运行时交执行线程同步处理；暂停期间拒绝提交。成功表示许可成立，不承诺已创建自主任务。
 * 自主睡眠要求三槽为空、无任何非终态任务且有容量，按精力阈值选择主通道；不寻找地点或重排日程。
 * 本批不支持运行期间撤权或搬离。
 * 加载 ACTIVE 睡眠须在 Start 前重新确认许可；此接口不能补授已过去时段的许可。
 */
extern DOMINO_CODE dominoEngineAuthorizeSleep(domino_human_id_t human_id, domino_building_id_t building_id);

/**
 * @brief 客户端确认睡眠许可并提交单行为睡眠，成功时输出任务 ID 并安排执行；失败保持输出不变。
 * @note Init 后尚未 Start 时仅提交 PLANNED，运行中由执行线程校验、提交并随后启动；暂停期间拒绝提交。成功同时保留后续自主睡眠许可。
 * 后续自主安排使用主通道，须满足完全空闲、无非终态任务及容量条件；不自动寻找地点。
 * 只支持同步睡眠内核的单活动形态，三个通道仍可任选。调用须与其他客户端生命周期和睡眠 API 串行。
 */
extern DOMINO_CODE dominoEngineSubmitSleep(domino_human_id_t human_id, domino_building_id_t building_id, uint8_t channel_index,
                                           domino_task_id_t* task_id_out_ptr);

/**
 * @brief 客户端向引擎主循环发送消息。
 * @param message_ptr 可写消息；成功后 id 为本次入队生成的 ID。
 * @note 只允许登记的客户端线程在 RUNNING 期间发送，冻结不影响入队；业务消费者在解冻后继续处理。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；满载、扩容失败、ID 耗尽等传输故障打印致命错误并退出进程，不等待空位。
 *         请求状态或线程不合法时记录非致命错误并返回；入队仍需获取队列 mutex。
 */
extern DOMINO_CODE dominoEnginePushMsg(DominoEngineMessage* message_ptr);

/**
 * @brief 客户端接收引擎消息。
 * @param blocking 正常状态下无消息时，true 等待消息，false 返回 `ERR_QUEUE_EMPTY`；两种模式均需获取队列 mutex。
 * @note 冻结不影响取出已有消息；暂停时空队列的阻塞调用仍等待生产者，需继续控制游戏时使用非阻塞模式。
 *       系统或同步故障直接打印致命错误并退出进程。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；引擎队列未初始化返回 `ERR_NOT_INITIALIZED`；
 *         队列已进入排空状态且为空时返回 `ERR_QUEUE_FLUSHING`，不再等待消息。
 */
extern DOMINO_CODE dominoEnginePopMsg(DominoEngineMessage* message_out_ptr, bool blocking);

#ifdef DOMINO_PLAYGROUND_MODE
extern int dominoEnginePlayground(int argc, char* argv[]);
#endif
#endif
