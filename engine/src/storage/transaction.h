#ifndef DOMINO_ENGINE_STORAGE_TRANSACTION_H
#define DOMINO_ENGINE_STORAGE_TRANSACTION_H

#include <stddef.h>

/** @file transaction.h
 * @brief 存档事务（宪法第五章）：完整写入同父目录的 staging，再按 active → .prev、staging → active 顺序 rename，
 * 每次关键目录项变更后刷新父目录；任一步中断后，下次加载前都能恢复到某次完整提交。
 * @note save_path 是引擎持有的已校验路径。active、staging 或 .prev 不是目录时拒绝处理，不递归删除冲突路径。
 * 提交所需的步骤失败一律致命退出，遗留状态交给下次启动恢复。
 */

/** @brief 确保存档目录的父目录树存在。 */
void storageTxnPrepareParent(const char* save_path);

/** @brief 生成本次保存的 staging 目录路径（同父目录，带时间戳和 PID）。 */
void storageTxnBuildStagingPath(const char* save_path, char* staging_out, size_t staging_cap);

/** @brief 加载前收敛事务状态：只有 .prev 时恢复为 active；两者都在时保留 active 并清理 .prev（清理失败只告警）。 */
void storageTxnRecover(const char* save_path);

/** @brief 把 staging 提交为 active；提交后清理旧备份，清理失败只告警到 stdout。 */
void storageTxnCommit(const char* staging_path, const char* save_path);

#endif
