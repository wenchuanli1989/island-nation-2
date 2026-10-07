#ifndef DOMINO_ENGINE_STORAGE_TRANSACTION_H
#define DOMINO_ENGINE_STORAGE_TRANSACTION_H

#include <stddef.h>

#include "domino_shared_error_codes.h"

/** @brief 确保存档目录的父目录树存在。 */
DOMINO_CODE txnPrepareSaveParent(const char* save_path);

/** @brief 根据正式存档路径生成本次保存使用的 staging 目录路径。 */
DOMINO_CODE txnBuildStagingPath(const char* save_path, char* staging_out, size_t staging_cap);

/**
 * @brief 在加载或保存前收敛 active/.prev 事务状态。
 *
 * active 缺失且 `.prev` 存在时恢复旧存档；两者同时存在时保留 active 并清理旧备份。
 * symlink、普通文件等非目录冲突会返回错误，不会递归删除。
 */
DOMINO_CODE txnRecoverSave(const char* save_path);

/** @brief 将 staging 目录提交为正式存档目录，失败时尽量恢复旧存档。 */
DOMINO_CODE txnCommitReplace(const char* staging_path, const char* save_path);

#endif
