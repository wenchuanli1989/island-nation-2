#define _POSIX_C_SOURCE 200809L

#include "transaction.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../logger/entry.h"
#include "domino_shared_common.h"
#include "entry.h"

typedef enum TxnPathKind {
    TXN_PATH_MISSING = 0,
    TXN_PATH_DIRECTORY,
    TXN_PATH_OTHER,
} TxnPathKind;

static DOMINO_CODE txnBuildBackupPath(const char* save_path, char* backup_out, size_t backup_cap) {
    int written = snprintf(backup_out, backup_cap, "%s.prev", save_path);
    if (written < 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to format backup path (path=%s)", save_path);
        return ERR_STRING_FORMAT;
    }
    if ((size_t)written >= backup_cap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "backup path exceeds output buffer (path=%s, capacity=%zu)",
                          save_path, backup_cap);
        return ERR_BUFFER_TOO_SMALL;
    }
    return CODE_OK;
}

static DOMINO_CODE txnGetPathKind(const char* path, TxnPathKind* kind_out) {
    struct stat path_stat;
    if (lstat(path, &path_stat) != 0) {
        if (errno == ENOENT) {
            *kind_out = TXN_PATH_MISSING;
            return CODE_OK;
        }
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to inspect save path (path=%s, errno=%d)", path, errno);
        return ERR_FILE_STAT;
    }
    *kind_out = S_ISDIR(path_stat.st_mode) ? TXN_PATH_DIRECTORY : TXN_PATH_OTHER;
    return CODE_OK;
}

static DOMINO_CODE txnFlushDirectory(const char* directory_path) {
    int directory_fd = open(directory_path, O_RDONLY | O_DIRECTORY);
    if (directory_fd < 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to open directory for flushing (path=%s, errno=%d)",
                          directory_path, errno);
        return ERR_DIRECTORY_OPEN;
    }
    if (fsync(directory_fd) != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to flush save directory (path=%s, errno=%d)",
                          directory_path, errno);
        return ERR_FILE_FLUSH;
    }
    if (close(directory_fd) != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to close save directory (path=%s, errno=%d)",
                          directory_path, errno);
        return ERR_DIRECTORY_CLOSE;
    }
    return CODE_OK;
}

static DOMINO_CODE txnFlushSaveParent(const char* save_path) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    char base[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, sizeof(parent), base, sizeof(base));
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to split save path (path=%s, code=%d)", save_path,
                          result);
        return result;
    }
    return txnFlushDirectory(parent);
}

DOMINO_CODE txnPrepareSaveParent(const char* save_path) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    char base[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, sizeof(parent), base, sizeof(base));
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to split save path (path=%s, code=%d)", save_path,
                          result);
        return result;
    }
    result = dominoMkdirRecursive(parent);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to create save parent (path=%s, code=%d)", parent,
                          result);
    }
    return result;
}

/* staging 与正式目录位于同一父目录，提交依赖 rename 与父目录 fsync。 */
DOMINO_CODE txnBuildStagingPath(const char* save_path, char* staging_out, size_t staging_cap) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    char base[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, sizeof(parent), base, sizeof(base));
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to split save path (path=%s, code=%d)", save_path,
                          result);
        return result;
    }
    uint64_t wall_timestamp_ms;  // UTC 时间戳仅用于事务目录命名，不是业务累计时钟。
    result = dominoWallTimeMsChecked(&wall_timestamp_ms);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to read transaction naming clock (code=%d)", result);
        return result;
    }
    uint64_t pid = dominoGetPid();
    int written;
    written = snprintf(staging_out, staging_cap, "%s/.%s.__txn__.%" PRIu64 "-%" PRIu64, parent, base, wall_timestamp_ms, pid);
    if (written < 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to format staging path (path=%s)", save_path);
        return ERR_STRING_FORMAT;
    }
    if ((size_t)written >= staging_cap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "staging path exceeds output buffer (path=%s, capacity=%zu)",
                          save_path, staging_cap);
        return ERR_BUFFER_TOO_SMALL;
    }
    return CODE_OK;
}

DOMINO_CODE txnRecoverSave(const char* save_path) {
    char backup_path[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = txnBuildBackupPath(save_path, backup_path, sizeof(backup_path));
    if (result != CODE_OK) {
        return result;
    }

    TxnPathKind active_kind;
    TxnPathKind backup_kind;
    result = txnGetPathKind(save_path, &active_kind);
    if (result != CODE_OK) {
        return result;
    }
    result = txnGetPathKind(backup_path, &backup_kind);
    if (result != CODE_OK) {
        return result;
    }

    if (active_kind == TXN_PATH_OTHER) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "active save path is not a directory (path=%s)", save_path);
        return ERR_PATH_TYPE;
    }
    if (backup_kind == TXN_PATH_OTHER) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "backup save path is not a directory (path=%s)", backup_path);
        return ERR_PATH_TYPE;
    }

    if (active_kind == TXN_PATH_MISSING) {
        if (backup_kind == TXN_PATH_MISSING) {
            return CODE_OK;
        }
        if (dominoRename(backup_path, save_path) != 0) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                              "failed to restore save backup (source=%s, target=%s, errno=%d)", backup_path, save_path, errno);
            return ERR_FILE_RENAME;
        }
        result = txnFlushSaveParent(save_path);
        if (result != CODE_OK) {
            return result;
        }
        return CODE_OK;
    }

    if (backup_kind == TXN_PATH_MISSING) {
        return CODE_OK;
    }

    /* active 已出现表示 staging rename 已完成；先刷父目录，再清理旧备份。 */
    result = txnFlushSaveParent(save_path);
    if (result != CODE_OK) {
        return result;
    }
    result = dominoRemovePathRecursive(backup_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to remove save backup (path=%s, code=%d)", backup_path,
                          result);
        return result;
    }
    result = txnFlushSaveParent(save_path);
    if (result != CODE_OK) {
        return result;
    }
    return CODE_OK;
}

DOMINO_CODE txnCommitReplace(const char* staging_path, const char* save_path) {
    char backup_path[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = txnBuildBackupPath(save_path, backup_path, sizeof(backup_path));
    if (result != CODE_OK) {
        return result;
    }
    result = txnRecoverSave(save_path);
    if (result != CODE_OK) {
        return result;
    }

    TxnPathKind staging_kind;
    TxnPathKind active_kind;
    result = txnGetPathKind(staging_path, &staging_kind);
    if (result != CODE_OK) {
        return result;
    }
    result = txnGetPathKind(save_path, &active_kind);
    if (result != CODE_OK) {
        return result;
    }
    if (staging_kind == TXN_PATH_MISSING) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "staged save directory is missing (path=%s)", staging_path);
        return ERR_FILE_NOT_FOUND;
    }
    if (staging_kind != TXN_PATH_DIRECTORY || active_kind == TXN_PATH_OTHER) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                          "invalid save transaction paths (staging=%s, kind=%d, active=%s, kind=%d)", staging_path, staging_kind, save_path,
                          active_kind);
        return ERR_PATH_TYPE;
    }

    result = txnFlushDirectory(staging_path);
    if (result != CODE_OK) {
        return result;
    }

    bool had_active_save = active_kind == TXN_PATH_DIRECTORY;
    if (had_active_save) {
        /* 先保留旧存档；后续失败交给下一次启动恢复，当前进程不回滚。 */
        if (dominoRename(save_path, backup_path) != 0) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to retain old save (source=%s, target=%s, errno=%d)",
                              save_path, backup_path, errno);
            return ERR_FILE_RENAME;
        }
        result = txnFlushSaveParent(save_path);
        if (result != CODE_OK) {
            return result;
        }
    }
    if (dominoRename(staging_path, save_path) != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to commit staged save (source=%s, target=%s, errno=%d)",
                          staging_path, save_path, errno);
        return ERR_FILE_RENAME;
    }

    result = txnFlushSaveParent(save_path);
    if (result != CODE_OK) {
        /* active 与 backup 同时保留，下一次 Init 会按提交完成状态收敛。 */
        return result;
    }

    /* 提交完成后清理旧备份；失败仍直接上报，保留已完成的磁盘步骤。 */
    if (had_active_save) {
        result = dominoRemovePathRecursive(backup_path);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to remove committed save backup (path=%s, code=%d)",
                              backup_path, result);
            return result;
        }
        result = txnFlushSaveParent(save_path);
        if (result != CODE_OK) {
            return result;
        }
    }
    return CODE_OK;
}
