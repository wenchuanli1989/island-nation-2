#ifndef DOMINO_ENGINE_STORAGE_TRANSACTION_H
#define DOMINO_ENGINE_STORAGE_TRANSACTION_H

#include <stddef.h>

/* 内部事务契约：save_path 是引擎持有的已校验路径，staging_path 由 txnBuildStagingPath 生成，输出缓冲区有效。 */

/** @brief 确保存档目录的父目录树存在；失败直接打印 stderr 并终止进程。 */
void txnPrepareSaveParent(const char* save_path);

/** @brief 根据正式存档路径生成本次保存使用的 staging 目录路径；失败直接打印 stderr 并终止进程。 */
void txnBuildStagingPath(const char* save_path, char* staging_out, size_t staging_cap);

/**
 * @brief 加载前收敛 active/.prev 事务状态。
 *
 * active 缺失且 `.prev` 存在时恢复旧存档；两者同时存在时保留 active 并清理旧备份。
 * symlink、普通文件等非目录冲突和恢复失败直接终止，不会递归删除冲突路径。
 * active 已持久化后，旧备份清理 I/O 失败只告警，允许继续加载 active。
 */
void txnRecoverSave(const char* save_path);

/**
 * @brief 将 staging 提交为正式存档；提交失败直接终止，遗留状态交给下次启动恢复。
 * @note 提交前恢复 active，并要求旧备份清理和目录刷新成功；提交完成后的备份清理 I/O 失败通过 DOMINO_PRINT_MESSAGE 告警到 stdout，不影响已提交存档。
 */
void txnCommitReplace(const char* staging_path, const char* save_path);

#endif
