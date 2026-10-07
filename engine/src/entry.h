#ifndef DOMINO_ENGINE_ENTRY_H
#define DOMINO_ENGINE_ENTRY_H

enum { DOMINO_STORAGE_ACCOUNT_ID_MAX = 128, DOMINO_STORAGE_RUN_MODE_MAX = 32 };

#include <stdatomic.h>
#include <stdbool.h>

#include "domino_engine.h"

/** @brief 存档 meta.info.account_id 的运行时缓存。 */
extern char g_domino_storage_account_id[DOMINO_STORAGE_ACCOUNT_ID_MAX];

/** @brief 存档 meta.runtime_option.run_mode 的运行时缓存，默认值为 "local"。 */
extern char g_domino_storage_run_mode[DOMINO_STORAGE_RUN_MODE_MAX];

/** @brief 初始化时保存的启动配置；storage_path 指向引擎持有的路径副本。 */
extern DominoEngineLaunchConfig g_domino_engine_launch_config;

/** @brief human behavior planning 线程停止标志，由主循环线程写入。 */
extern _Atomic bool g_domino_human_behavior_planning_stop;

/** @brief 当前线程是否为客户端线程，且引擎处于 INITIALIZED 或 RUNNING。 */
extern bool dominoEngineIsClientThread(void);

/**
 * @brief 引擎工作线程向客户端发送消息。
 * @param message_ptr 可写消息；成功后 id 为本次入队生成的 ID。
 * @param blocking 达到最大容量时，true 等待空位，false 返回 `ERR_QUEUE_FULL`。
 */
extern DOMINO_CODE dominoLoopProduceToMain(DominoEngineMessage* message_ptr, bool blocking);

#endif
