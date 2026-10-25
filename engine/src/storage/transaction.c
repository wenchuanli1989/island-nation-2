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

#include "domino_shared_common.h"
#include "entry.h"

typedef enum TxnPathKind {
    TXN_PATH_MISSING = 0,
    TXN_PATH_DIRECTORY,
    TXN_PATH_OTHER,
} TxnPathKind;

static void txnBuildBackupPath(const char* save_path, char* backup_out, size_t backup_cap) {
    int written = snprintf(backup_out, backup_cap, "%s.prev", save_path);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format backup path (path=%s)", save_path);
    }
    if ((size_t)written >= backup_cap) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "backup path exceeds output buffer (path=%s, capacity=%zu)", save_path, backup_cap);
    }
}

static TxnPathKind txnGetPathKind(const char* path) {
    struct stat path_stat;
    if (lstat(path, &path_stat) != 0) {
        if (errno == ENOENT) {
            return TXN_PATH_MISSING;
        }
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_STAT, "failed to inspect save path (path=%s, errno=%d)", path, errno);
    }
    return S_ISDIR(path_stat.st_mode) ? TXN_PATH_DIRECTORY : TXN_PATH_OTHER;
}

/* 保留结果，由调用边界区分提交所需刷新与已提交备份的非致命清理。 */
static DOMINO_CODE txnTryFlushDirectory(const char* directory_path) {
    int directory_fd = open(directory_path, O_RDONLY | O_DIRECTORY);
    if (directory_fd < 0) {
        return ERR_DIRECTORY_OPEN;
    }
    if (fsync(directory_fd) != 0) {
        int flush_errno = errno;
        (void)close(directory_fd);
        errno = flush_errno;
        return ERR_FILE_FLUSH;
    }
    if (close(directory_fd) != 0) {
        return ERR_DIRECTORY_CLOSE;
    }
    return CODE_OK;
}

static void txnFlushDirectory(const char* directory_path) {
    DOMINO_CODE result = txnTryFlushDirectory(directory_path);
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to flush save directory (path=%s, errno=%d)", directory_path, errno);
    }
}

static void txnGetSaveParent(const char* save_path, char parent[DOMINO_STORAGE_PATH_MAX]) {
    char base[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, DOMINO_STORAGE_PATH_MAX, base, sizeof(base));
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to split save path (path=%s)", save_path);
    }
}

static void txnFlushSaveParent(const char* save_path) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    txnGetSaveParent(save_path, parent);
    txnFlushDirectory(parent);
}

/* 不打印：加载和提交后的清理允许告警，开始下一次提交前的清理必须成功。 */
static DOMINO_CODE txnCleanupBackup(const char* save_path, const char* backup_path) {
    DOMINO_CODE result = dominoRemovePathRecursive(backup_path);
    if (result != CODE_OK) {
        return result;
    }
    char parent[DOMINO_STORAGE_PATH_MAX];
    txnGetSaveParent(save_path, parent);
    return txnTryFlushDirectory(parent);
}

void txnPrepareSaveParent(const char* save_path) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    txnGetSaveParent(save_path, parent);
    DOMINO_CODE result = dominoMkdirRecursive(parent);
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to create save parent (path=%s)", parent);
    }
}

/* staging 与正式目录位于同一父目录，提交依赖 rename 与父目录 fsync。 */
void txnBuildStagingPath(const char* save_path, char* staging_out, size_t staging_cap) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    char base[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, sizeof(parent), base, sizeof(base));
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to split save path (path=%s)", save_path);
    }
    uint64_t wall_timestamp_ms = dominoWallTimeMs();  // UTC 时间戳仅用于事务目录命名，不是业务累计时钟。
    uint64_t pid = dominoGetPid();
    int written;
    written = snprintf(staging_out, staging_cap, "%s/.%s.__txn__.%" PRIu64 "-%" PRIu64, parent, base, wall_timestamp_ms, pid);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format staging path (path=%s)", save_path);
    }
    if ((size_t)written >= staging_cap) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "staging path exceeds output buffer (path=%s, capacity=%zu)", save_path, staging_cap);
    }
}

/** @brief 恢复并持久化 active；返回是否还有已提交的旧备份需要清理。 */
static bool txnRecoverActiveSave(const char* save_path, const char* backup_path) {
    TxnPathKind active_kind = txnGetPathKind(save_path);
    TxnPathKind backup_kind = txnGetPathKind(backup_path);

    if (active_kind == TXN_PATH_OTHER) {
        DOMINO_EXIT_WITH_ERROR(ERR_PATH_TYPE, "active save path is not a directory (path=%s)", save_path);
    }
    if (backup_kind == TXN_PATH_OTHER) {
        DOMINO_EXIT_WITH_ERROR(ERR_PATH_TYPE, "backup save path is not a directory (path=%s)", backup_path);
    }

    if (active_kind == TXN_PATH_MISSING) {
        if (backup_kind == TXN_PATH_MISSING) {
            return false;
        }
        if (dominoRename(backup_path, save_path) != 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_RENAME, "failed to restore save backup (source=%s, target=%s, errno=%d)", backup_path, save_path, errno);
        }
        txnFlushSaveParent(save_path);
        return false;
    }

    if (backup_kind == TXN_PATH_MISSING) {
        return false;
    }

    /* active 已出现表示 staging rename 已完成；先刷父目录，调用方才能清理旧备份。 */
    txnFlushSaveParent(save_path);
    return true;
}

void txnRecoverSave(const char* save_path) {
    char backup_path[DOMINO_STORAGE_PATH_MAX];
    txnBuildBackupPath(save_path, backup_path, sizeof(backup_path));
    if (txnRecoverActiveSave(save_path, backup_path)) {
        DOMINO_CODE result = txnCleanupBackup(save_path, backup_path);
        if (result != CODE_OK) {
            DOMINO_PRINT_MESSAGE("failed to clean up committed save backup (path=%s, errno=%d, code=%d)", backup_path, errno, result);
        }
    }
}

void txnCommitReplace(const char* staging_path, const char* save_path) {
    char backup_path[DOMINO_STORAGE_PATH_MAX];
    txnBuildBackupPath(save_path, backup_path, sizeof(backup_path));
    if (txnRecoverActiveSave(save_path, backup_path)) {
        DOMINO_CODE result = txnCleanupBackup(save_path, backup_path);
        if (result != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(result, "failed to clean up previous save backup before commit (path=%s, errno=%d)", backup_path, errno);
        }
    }
    TxnPathKind staging_kind = txnGetPathKind(staging_path);
    TxnPathKind active_kind = txnGetPathKind(save_path);
    if (staging_kind == TXN_PATH_MISSING) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_NOT_FOUND, "staged save directory is missing (path=%s)", staging_path);
    }
    if (staging_kind != TXN_PATH_DIRECTORY || active_kind == TXN_PATH_OTHER) {
        DOMINO_EXIT_WITH_ERROR(ERR_PATH_TYPE, "invalid save transaction paths (staging=%s, kind=%d, active=%s, kind=%d)", staging_path, staging_kind,
                               save_path, active_kind);
    }

    txnFlushDirectory(staging_path);

    bool had_active_save = active_kind == TXN_PATH_DIRECTORY;
    if (had_active_save) {
        /* 先保留旧存档；后续失败交给下一次启动恢复，当前进程不回滚。 */
        if (dominoRename(save_path, backup_path) != 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_RENAME, "failed to retain old save (source=%s, target=%s, errno=%d)", save_path, backup_path, errno);
        }
        txnFlushSaveParent(save_path);
    }
    if (dominoRename(staging_path, save_path) != 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_RENAME, "failed to commit staged save (source=%s, target=%s, errno=%d)", staging_path, save_path, errno);
    }

    /* 刷新失败时保留 active 与 backup，下一次 Init 会按提交完成状态收敛。 */
    txnFlushSaveParent(save_path);

    /* 提交完成后清理旧备份；清理 I/O 失败只告警，保留已提交的 active。 */
    if (had_active_save) {
        DOMINO_CODE result = txnCleanupBackup(save_path, backup_path);
        if (result != CODE_OK) {
            DOMINO_PRINT_MESSAGE("failed to clean up committed save backup (path=%s, errno=%d, code=%d)", backup_path, errno, result);
        }
    }
}
