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

typedef enum STORAGE_PATH_KIND {
    PATH_KIND_MISSING = 0,
    PATH_KIND_DIRECTORY,
    PATH_KIND_OTHER,
} STORAGE_PATH_KIND;

static void buildBackupPath(const char* save_path, char* backup_out, size_t backup_cap) {
    int written = snprintf(backup_out, backup_cap, "%s.prev", save_path);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format backup path (path=%s)", save_path);
    }
    if ((size_t)written >= backup_cap) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "backup path exceeds output buffer (path=%s, capacity=%zu)", save_path, backup_cap);
    }
}

static STORAGE_PATH_KIND getPathKind(const char* path) {
    struct stat path_stat;
    if (lstat(path, &path_stat) != 0) {
        if (errno == ENOENT) {
            return PATH_KIND_MISSING;
        }
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_STAT, "failed to inspect save path (path=%s, errno=%d)", path, errno);
    }
    return S_ISDIR(path_stat.st_mode) ? PATH_KIND_DIRECTORY : PATH_KIND_OTHER;
}

/** @brief fsync 目录；返回结果而不退出，调用方区分必须成功的刷新和可以只告警的清理。 */
static DOMINO_CODE tryFlushDirectory(const char* directory_path) {
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

static void flushDirectory(const char* directory_path) {
    DOMINO_CODE result = tryFlushDirectory(directory_path);
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to flush save directory (path=%s, errno=%d)", directory_path, errno);
    }
}

static void getSaveParent(const char* save_path, char parent[DOMINO_STORAGE_PATH_MAX]) {
    char base[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, DOMINO_STORAGE_PATH_MAX, base, sizeof(base));
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to split save path (path=%s)", save_path);
    }
}

static void flushSaveParent(const char* save_path) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    getSaveParent(save_path, parent);
    flushDirectory(parent);
}

/** @brief 删除旧备份并刷新父目录；不打印，由调用方决定告警还是致命退出。 */
static DOMINO_CODE cleanupBackup(const char* save_path, const char* backup_path) {
    DOMINO_CODE result = dominoRemovePathRecursive(backup_path);
    if (result != CODE_OK) {
        return result;
    }
    char parent[DOMINO_STORAGE_PATH_MAX];
    getSaveParent(save_path, parent);
    return tryFlushDirectory(parent);
}

void storageTxnPrepareParent(const char* save_path) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    getSaveParent(save_path, parent);
    DOMINO_CODE result = dominoMkdirRecursive(parent);
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to create save parent (path=%s)", parent);
    }
}

// staging 与正式目录同父目录，提交才能依靠 rename 与父目录 fsync。
void storageTxnBuildStagingPath(const char* save_path, char* staging_out, size_t staging_cap) {
    char parent[DOMINO_STORAGE_PATH_MAX];
    char base[DOMINO_STORAGE_PATH_MAX];
    DOMINO_CODE result = dominoPathSplitParentBase(save_path, parent, sizeof(parent), base, sizeof(base));
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to split save path (path=%s)", save_path);
    }
    uint64_t wall_timestamp_ms = dominoGetWallTimeMs();  // 只用于命名，不是业务时间。
    uint64_t pid = dominoGetPid();
    int written = snprintf(staging_out, staging_cap, "%s/.%s.__txn__.%" PRIu64 "-%" PRIu64, parent, base, wall_timestamp_ms, pid);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format staging path (path=%s)", save_path);
    }
    if ((size_t)written >= staging_cap) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "staging path exceeds output buffer (path=%s, capacity=%zu)", save_path, staging_cap);
    }
}

/** @brief 收敛 active 与 .prev：只有 .prev 时改名恢复为 active。
 * @return 两者同时存在（staging 已提交、旧备份待清理）时返回 true。
 */
static bool recoverActiveSave(const char* save_path, const char* backup_path) {
    STORAGE_PATH_KIND active_kind = getPathKind(save_path);
    STORAGE_PATH_KIND backup_kind = getPathKind(backup_path);

    if (active_kind == PATH_KIND_OTHER) {
        DOMINO_EXIT_WITH_ERROR(ERR_PATH_TYPE, "active save path is not a directory (path=%s)", save_path);
    }
    if (backup_kind == PATH_KIND_OTHER) {
        DOMINO_EXIT_WITH_ERROR(ERR_PATH_TYPE, "backup save path is not a directory (path=%s)", backup_path);
    }

    if (active_kind == PATH_KIND_MISSING) {
        if (backup_kind == PATH_KIND_MISSING) {
            return false;
        }
        if (dominoRename(backup_path, save_path) != 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_RENAME, "failed to restore save backup (source=%s, target=%s, errno=%d)", backup_path, save_path, errno);
        }
        flushSaveParent(save_path);
        return false;
    }

    if (backup_kind == PATH_KIND_MISSING) {
        return false;
    }

    // active 与 .prev 并存说明上次提交已完成；先刷新父目录，再由调用方清理旧备份。
    flushSaveParent(save_path);
    return true;
}

void storageTxnRecover(const char* save_path) {
    char backup_path[DOMINO_STORAGE_PATH_MAX];
    buildBackupPath(save_path, backup_path, sizeof(backup_path));
    if (recoverActiveSave(save_path, backup_path)) {
        DOMINO_CODE result = cleanupBackup(save_path, backup_path);
        if (result != CODE_OK) {
            DOMINO_PRINT_MESSAGE("failed to clean up committed save backup (path=%s, errno=%d, code=%d)", backup_path, errno, result);
        }
    }
}

void storageTxnCommit(const char* staging_path, const char* save_path) {
    char backup_path[DOMINO_STORAGE_PATH_MAX];
    buildBackupPath(save_path, backup_path, sizeof(backup_path));
    if (recoverActiveSave(save_path, backup_path)) {
        DOMINO_CODE result = cleanupBackup(save_path, backup_path);
        if (result != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(result, "failed to clean up previous save backup before commit (path=%s, errno=%d)", backup_path, errno);
        }
    }
    STORAGE_PATH_KIND staging_kind = getPathKind(staging_path);
    STORAGE_PATH_KIND active_kind = getPathKind(save_path);
    if (staging_kind == PATH_KIND_MISSING) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_NOT_FOUND, "staged save directory is missing (path=%s)", staging_path);
    }
    if (staging_kind != PATH_KIND_DIRECTORY || active_kind == PATH_KIND_OTHER) {
        DOMINO_EXIT_WITH_ERROR(ERR_PATH_TYPE, "invalid save transaction paths (staging=%s, kind=%d, active=%s, kind=%d)", staging_path, staging_kind,
                               save_path, active_kind);
    }

    flushDirectory(staging_path);

    bool had_active_save = active_kind == PATH_KIND_DIRECTORY;
    if (had_active_save) {
        // 先保留旧存档；之后失败由下次启动恢复，当前进程不回滚。
        if (dominoRename(save_path, backup_path) != 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_RENAME, "failed to retain old save (source=%s, target=%s, errno=%d)", save_path, backup_path, errno);
        }
        flushSaveParent(save_path);
    }
    if (dominoRename(staging_path, save_path) != 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_RENAME, "failed to commit staged save (source=%s, target=%s, errno=%d)", staging_path, save_path, errno);
    }

    flushSaveParent(save_path);

    // 提交已完成，旧备份清理失败只告警。
    if (had_active_save) {
        DOMINO_CODE result = cleanupBackup(save_path, backup_path);
        if (result != CODE_OK) {
            DOMINO_PRINT_MESSAGE("failed to clean up committed save backup (path=%s, errno=%d, code=%d)", backup_path, errno, result);
        }
    }
}
