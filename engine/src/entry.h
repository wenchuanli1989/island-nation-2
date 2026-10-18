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
 * @pre 调用方已校验接口允许的生命周期状态，确保本次 Init 已成功且尚未完成 Exit。
 */
extern bool dominoEngineIsClientThread(void);

/** @brief 查询正常退出请求；业务线程仅在批次边界观察，当前批次仍可向存活队列投递。 */
extern bool dominoEngineIsStopRequested(void);

/** @brief 保存本次实例首个失败并唤醒主循环及客户端消息等待者；可由任意工作线程调用。
 * @note 不记录日志、不取得业务模块锁、不 join 或释放资源；后续业务入口拒绝继续运行。
 */
extern DOMINO_CODE dominoEngineReportFailure(DOMINO_CODE code);

/**
 * @brief 引擎工作线程向客户端发送消息。
 * @param message_ptr 可写消息；成功后 id 为本次入队生成的 ID。
 * @return 入队成功返回 CODE_OK；队列满等传输错误终结实例，不等待空位。
 */
extern DOMINO_CODE dominoLoopProduceToMain(DominoEngineMessage* message_ptr);

#endif
