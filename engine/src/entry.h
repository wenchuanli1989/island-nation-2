#ifndef DOMINO_ENGINE_ENTRY_H
#define DOMINO_ENGINE_ENTRY_H

enum { DOMINO_STORAGE_ACCOUNT_ID_MAX = 128, DOMINO_STORAGE_RUN_MODE_MAX = 32 };

#include <stdbool.h>

#include "domino_engine.h"

/** @brief 存档 meta.info.account_id 的运行时缓存。 */
extern char g_domino_storage_account_id[DOMINO_STORAGE_ACCOUNT_ID_MAX];

/** @brief 存档 meta.runtime_option.run_mode 的运行时缓存，默认值为 "local"。 */
extern char g_domino_storage_run_mode[DOMINO_STORAGE_RUN_MODE_MAX];

/** @brief 初始化时保存的启动配置；storage_path 指向引擎持有的路径副本。 */
extern DominoEngineLaunchConfig g_domino_engine_launch_config;

/** @brief 判断当前线程是否为 Init 登记的客户端线程。
 * @pre 调用方已校验接口允许的生命周期状态，确保本次 Init 已成功且尚未完成 Destroy。
 */
extern bool dominoEngineIsClientThread(void);

/** @brief 查询生命周期是否处于 STOPPING；业务线程仅在批次边界观察，当前批次仍可向存活队列投递。
 * @note 运行过的实例在业务线程 join、人物结算和日志排空后进入 STOPPED；仅 Init 的实例跳过所有 join 和结算，logger 关闭入队并刷新。
 * 资源留到 Destroy 释放；其他生命周期状态均返回 false。
 */
extern bool dominoEngineIsStopping(void);

/** @brief 查询是否处于 RUNNING，供已通过引擎访问检查的业务入口选择同步处理或线程投递。 */
extern bool dominoEngineIsRunning(void);

/**
 * @brief 引擎工作线程向客户端发送消息。
 * @pre 引擎已准备客户端收件队列，并在全部投递线程 join 前保持队列有效；本接口不重复校验生命周期。
 * @param message_ptr 可写消息；成功后 id 为本次入队生成的 ID。
 * @note 队列满等传输故障直接打印致命错误并退出进程，不等待空位。
 */
extern void dominoLoopProduceToMain(DominoEngineMessage* message_ptr);

#endif
