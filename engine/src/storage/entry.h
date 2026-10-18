#ifndef DOMINO_ENGINE_STORAGE_MODULE_H
#define DOMINO_ENGINE_STORAGE_MODULE_H

#define DOMINO_STORAGE_SAVE_VERSION 12
#define DOMINO_STORAGE_PATH_MAX 1024

#include "domino_shared_error_codes.h"

/**
 * @brief 初始化 storage 子系统，从运行时配置指定的存档目录恢复内存状态。
 *
 * 由引擎生命周期调用；storage_path 已在 dominoEngineInit 中校验并复制。
 * 无存档文件时返回 CODE_OK，表示以新世界启动；存档存在但格式或完整性校验失败时返回错误码。
 * 失败不撤销已加载状态或释放临时资源，由宿主结束当前进程。
 */
extern DOMINO_CODE dominoStorageModuleInit(void);

/**
 * @brief 退出 storage 子系统，将当前内存状态事务式保存到存档目录。
 *
 * 由引擎生命周期调用；storage_path 使用初始化时已校验的副本。
 * 保存会先写入 staging 目录，所有分片和 meta 都成功后再替换正式存档目录。
 * 失败不删除 staging、不回滚已完成步骤，也不释放临时资源；由宿主决定何时结束当前进程，下次启动恢复磁盘事务。
 */
extern DOMINO_CODE dominoStorageModuleExit(void);

#endif
