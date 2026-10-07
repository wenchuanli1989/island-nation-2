#ifndef DOMINO_ENGINE_STORAGE_MODULE_H
#define DOMINO_ENGINE_STORAGE_MODULE_H

#define DOMINO_STORAGE_SAVE_VERSION 5
#define DOMINO_STORAGE_PATH_MAX 1024

#include "domino_shared_error_codes.h"

/**
 * @brief 初始化 storage 子系统，从运行时配置指定的存档目录恢复内存状态。
 *
 * 无存档文件时返回 CODE_OK，表示以新世界启动；存档存在但格式或完整性校验失败时返回错误码。
 */
extern DOMINO_CODE dominoStorageModuleInit(void);

/**
 * @brief 退出 storage 子系统，将当前内存状态事务式保存到存档目录。
 *
 * 保存会先写入 staging 目录，所有分片和 meta 都成功后再替换正式存档目录。
 */
extern DOMINO_CODE dominoStorageModuleExit(void);

#endif
