#ifndef DOMINO_ENGINE_H
#define DOMINO_ENGINE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "domino_engine_protocol.h"
#include "domino_shared_error_codes.h"

/** @file domino_engine.h
 * @brief 引擎对外接口：生命周期、冻结／解冻和客户端消息收发。消息结构定义在 domino_engine_protocol.h。
 * @note 生命周期只允许 Init → Start → Stop → Destroy 或 Init → Stop → Destroy，Destroy 后可重新 Init。
 * 全部接口由 Init 登记的客户端线程串行调用；错序、重复调用、错误线程及不可恢复错误直接致命退出（stderr）。
 * 生命周期和冻结操作的非致命诊断直接输出 stdout，运行期业务日志进入日志模块。
 * 引擎不接受客户端提交的任务或规划，也不提供通用的内部数据读写（宪法第十章）。
 */

typedef struct DominoEngineLaunchConfig {
    /** @brief 存档目录（必填）。Init 复制路径并去掉末尾 '/'，不接受根目录及末级为 . 或 .. 的路径。
     * 只有根 meta.json 不存在时以新世界启动；其他读取失败致命退出。
     */
    const char* storage_path;

    /** @brief 下次保存是否记录分片和模块 meta 的 CRC32C/size；加载按存档自身声明校验。 */
    bool storage_integrity_check;
} DominoEngineLaunchConfig;

/** @brief 准备固定资源并加载世界，保持时钟冻结、不创建线程；调用线程登记为客户端线程。
 * @note 返回时运行所需的初始数据已就绪（Nav 邻接、human 首次调度准备），但不推进任何业务。
 */
extern void dominoEngineInit(DominoEngineLaunchConfig launch_config);

typedef struct DominoEngineClientData {
    uint64_t runtime_base_ns;  ///< Start 时的累计未冻结现实纳秒，之后不更新。
    uint8_t reserved[4088];    ///< 填充至 4096 字节。
} DominoEngineClientData;
static_assert(sizeof(DominoEngineClientData) == 4096U, "client data must be 4096 bytes");

/** @brief 在冻结状态下依次启动 logger、human 执行器与三个规划线程、game_timer 和主循环，全部就绪后进入 RUNNING 并解冻。
 * @param client_data_out_ptr 可为空；非空时只写入 runtime_base_ns。
 * @note 返回只表示线程已启动，不表示规划或任务已执行。
 */
extern void dominoEngineStart(DominoEngineClientData* client_data_out_ptr);

/** @brief 进入 STOPPING，回收全部业务线程并把人物统一结算到停钟时刻，最后排空日志；成功后进入 STOPPED。
 * @pre 引擎处于 INITIALIZED 或 RUNNING（含冻结中）。仅 Init 的实例不 join、不结算，只停止日志。
 * @note 世界、队列和同步对象保留到 Destroy；尚未保存。STOPPED 后只能 Destroy。
 */
extern void dominoEngineStop(void);

/** @brief 保存世界并释放全部资源，回到未初始化状态；不停止或 join 任何线程。
 * @pre 引擎处于 STOPPED。
 * @note 保存前由 Nav DestroyBefore 规整存档布局，不再推进业务。保存失败致命退出；内存时钟清空不影响存档中的累计时间。
 */
extern void dominoEngineDestroy(void);

/** @brief 冻结业务时钟；重复冻结保持原时刻。只允许在 RUNNING 时调用。
 * @note 不通知、不等待业务线程：已开始的批次可以完成，留队消息解冻后继续处理。冻结不代表世界已停止写入，不能作为保存依据。
 */
extern void dominoEngineFreezeTime(void);

/** @brief 解冻业务时钟并唤醒主循环和业务线程。只允许在 RUNNING 且已冻结时调用。 */
extern void dominoEngineThawTime(void);

/** @brief 向主循环发送一条消息，成功后 message_ptr->id 为本次入队的 ID。只允许在 RUNNING 时调用，冻结不影响入队。
 * @note 当前未定义任何业务场景：主循环把所有消息按不支持的类型记录一次并丢弃，不回包。
 * @return CODE_OK 成功；空指针 ERR_NULL_POINTER；未初始化 ERR_NOT_INITIALIZED；其他状态或线程不合法 ERR_GAME_STATE_INVALID。
 *         拒绝时记录一次非致命诊断；队列满等传输故障致命退出，不等待空位。
 */
extern DOMINO_CODE dominoEngineSendMessage(DominoEngineMessage* message_ptr);

/** @brief 接收一条引擎消息。只允许在 RUNNING 时调用；当前没有会产生回包的场景，收件队列保持为空。
 * @param blocking 无消息时 true 等待，false 返回 ERR_QUEUE_EMPTY；冻结期间需要继续操作时应使用非阻塞模式。
 * @return 拒绝条件和诊断同 dominoEngineSendMessage。
 */
extern DOMINO_CODE dominoEngineReceiveMessage(DominoEngineMessage* message_out_ptr, bool blocking);

#ifdef DOMINO_PLAYGROUND_MODE
/** @brief 模块内部夹具入口：`npc_ai <sandbox_dir> <work_dir>` 运行 NPC AI 验证，其余参数运行导航演示。 */
extern int dominoEnginePlayground(int argc, char* argv[]);
#endif
#endif
