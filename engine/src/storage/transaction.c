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
    if (!save_path || save_path[0] == '\0' || !backup_out || backup_cap == 0U) {
        return ERR_INVALID_PARAM;
    }
    int written = snprintf(backup_out, backup_cap, "%s.prev", save_path);
    if (written < 0 || (size_t)written >= backup_cap) {
        return ERR_OUT_OF_RANGE;
    }
    return CODE_OK;
}

static DOMINO_CODE txnGetPathKind(const char* path, TxnPathKind* kind_out) {
    if (!path || path[0] == '\0' || !kind_out) {
        return ERR_INVALID_PARAM;
    }

    struct stat path_stat;
    if (lstat(path, &path_stat) != 0) {
        if (errno == ENOENT) {
            *kind_out = TXN_PATH_MISSING;
            return CODE_OK;
        }
        return ERR_FILE_READ;
    }
    *kind_out = S_ISDIR(path_stat.st_mode) ? TXN_PATH_DIRECTORY : TXN_PATH_OTHER;
    return CODE_OK;
}

static DOMINO_CODE txnFlushDirectory(const char* directory_path) {
    int directory_fd = open(directory_path, O_RDONLY | O_DIRECTORY);
    if (directory_fd < 0) {
        return ERR_FILE_OPEN;
    }
    int sync_result = fsync(directory_fd);
    int close_result = close(directory_fd);
    if (sync_result != 0 || close_result != 0) {
        return ERR_FILE_FLUSH;
    }
    return CODE_OK;
}

static DOMINO_CODE txnFlushSaveParent(const char* save_path) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    char base[512];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, sizeof(parent), base, sizeof(base));
    if (result != CODE_OK || base[0] == '\0') {
        return ERR_INVALID_PARAM;
    }
    return txnFlushDirectory(parent);
}

static DOMINO_CODE txnRollbackBackup(const char* backup_path, const char* save_path) {
    if (dominoRename(backup_path, save_path) != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn rollback backup failed: %s -> %s", backup_path, save_path);
        return ERR_FILE_RENAME;
    }
    DOMINO_CODE result = txnFlushSaveParent(save_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn rollback parent flush failed: %s", save_path);
    }
    return result;
}

DOMINO_CODE txnPrepareSaveParent(const char* save_path) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    char base[512];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, sizeof(parent), base, sizeof(base));
    if (result != CODE_OK || base[0] == '\0') {
        return ERR_INVALID_PARAM;
    }
    return dominoMkdirRecursive(parent);
}

/* staging 与正式目录位于同一父目录，提交依赖 rename 与父目录 fsync。 */
DOMINO_CODE txnBuildStagingPath(const char* save_path, char* staging_out, size_t staging_cap) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    char base[512];
    if (dominoPathSplitParentBase(save_path, parent, sizeof(parent), base, sizeof(base)) != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn build staging path failed: ");
        return ERR_INVALID_PARAM;
    }
    if (base[0] == '\0') {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn build staging path failed: base is empty");
        return ERR_INVALID_PARAM;
    }
    uint64_t timestamp_ms = dominoWallTimeMs();
    uint64_t pid = dominoGetPid();
    int written;
    written = snprintf(staging_out, staging_cap, "%s/.%s.__txn__.%" PRIu64 "-%" PRIu64, parent, base, timestamp_ms, pid);
    if (written < 0 || (size_t)written >= staging_cap) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn build staging path failed: buffer overflow");
        return ERR_INVALID_PARAM;
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
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn recovery refused non-directory active path: %s", save_path);
        return ERR_INVALID_DATA;
    }
    if (backup_kind == TXN_PATH_OTHER) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn recovery refused non-directory backup path: %s",
                          backup_path);
        return ERR_INVALID_DATA;
    }

    if (active_kind == TXN_PATH_MISSING) {
        if (backup_kind == TXN_PATH_MISSING) {
            return CODE_OK;
        }
        if (dominoRename(backup_path, save_path) != 0) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn recovery restore failed: %s -> %s", backup_path,
                              save_path);
            return ERR_FILE_RENAME;
        }
        result = txnFlushSaveParent(save_path);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn recovery parent flush failed: %s", save_path);
            return result;
        }
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_WARN, "txn recovered previous save: %s", save_path);
        return CODE_OK;
    }

    if (backup_kind == TXN_PATH_MISSING) {
        return CODE_OK;
    }

    /* active 已出现表示 staging rename 已完成；先刷父目录，再清理旧备份。 */
    result = txnFlushSaveParent(save_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn recovery active flush failed: %s", save_path);
        return result;
    }
    if (dominoRemovePathRecursive(backup_path) != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn recovery stale backup cleanup failed: %s", backup_path);
        return ERR_FILE_WRITE;
    }
    result = txnFlushSaveParent(save_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn recovery cleanup flush failed: %s", save_path);
        return result;
    }
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_WARN, "txn removed stale backup after committed save: %s", backup_path);
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
    if (staging_kind != TXN_PATH_DIRECTORY || active_kind == TXN_PATH_OTHER) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn commit requires real staging/active directories");
        return ERR_INVALID_DATA;
    }

    result = txnFlushDirectory(staging_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn staging directory flush failed: %s", staging_path);
        return result;
    }

    bool had_active_save = active_kind == TXN_PATH_DIRECTORY;
    if (had_active_save) {
        /* 先保留旧存档，使 staging -> save_path 失败时仍有回滚来源。 */
        if (dominoRename(save_path, backup_path) != 0) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn rename active save to backup failed: %s -> %s",
                              save_path, backup_path);
            return ERR_FILE_RENAME;
        }
        result = txnFlushSaveParent(save_path);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn backup rename flush failed: %s", backup_path);
            (void)txnRollbackBackup(backup_path, save_path);
            return result;
        }
    }
    if (dominoRename(staging_path, save_path) != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn rename staging to active save failed: %s -> %s",
                          staging_path, save_path);
        if (had_active_save) {
            (void)txnRollbackBackup(backup_path, save_path);
        }
        return ERR_FILE_RENAME;
    }

    result = txnFlushSaveParent(save_path);
    if (result != CODE_OK) {
        /* active 与 backup 同时保留，下一次 Init/保存会按提交完成状态收敛。 */
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn active rename flush failed: %s", save_path);
        return result;
    }

    /* 提交成功后删除备份目录，删除失败不改变本次保存成功语义。 */
    if (had_active_save && dominoRemovePathRecursive(backup_path) != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_WARN, "txn committed but backup cleanup failed: %s", backup_path);
        return CODE_OK;
    }
    if (had_active_save && txnFlushSaveParent(save_path) != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_WARN, "txn committed but backup cleanup flush failed: %s", backup_path);
    }
    return CODE_OK;
}
