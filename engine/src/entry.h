#ifndef DOMINO_ENGINE_ENTRY_H
#define DOMINO_ENGINE_ENTRY_H

enum { DOMINO_ACCOUNT_ID_SIZE = 128, DOMINO_RUN_MODE_SIZE = 32 };

#include <stdbool.h>

#include "domino_engine.h"

/** @file entry.h
 * @brief 引擎实例状态的内部入口：启动配置、存档账户信息、生命周期查询和向客户端发消息。
 */

/** @brief 存档 meta.info.account_id，由 storage 加载和保存。 */
extern char g_domino_account_id[DOMINO_ACCOUNT_ID_SIZE];

/** @brief 存档 meta.runtime_option.run_mode，默认 "local"。 */
extern char g_domino_run_mode[DOMINO_RUN_MODE_SIZE];

/** @brief Init 保存的启动配置；storage_path 指向引擎持有的已校验副本。 */
extern DominoEngineLaunchConfig g_domino_engine_launch_config;

/** @brief 当前线程是否为 Init 登记的客户端线程。 */
extern bool dominoEngineIsClientThread(void);

/** @brief 引擎是否处于 STOPPING；业务线程在批次边界据此退出，退出前的末批仍可向队列投递。 */
extern bool dominoEngineIsStopping(void);

/** @brief 引擎线程向客户端收件队列发送消息，成功后回写消息 ID；当前没有调用方。
 * @note 队列满等传输故障致命退出，不等待空位。
 */
extern void dominoEngineSendToClient(DominoEngineMessage* message_ptr);

#endif
