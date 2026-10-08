#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "domino_shared_common.h"
#include "domino_shared_error_codes.h"

uint64_t dominoGetPid(void) {
    return (uint64_t)getpid();
}

int dominoFlushFile(FILE* file_ptr) {
    int result = fflush(file_ptr);
    if (result != 0) {
        return result;
    }
    return fsync(fileno(file_ptr));
}

int dominoRename(const char* tmp_path, const char* final_path) {
    return rename(tmp_path, final_path);
}

DOMINO_CODE dominoAtomicWriteFile(const char* path, const void* data, size_t len) {
    if (!path || path[0] == '\0' || !data || len == 0U) {
        return ERR_INVALID_PARAM;
    }
    uint64_t pid = dominoGetPid();
    uint64_t wall_timestamp_ms = dominoWallTimeMs();  // UTC 时间戳仅用于文件命名，不是业务累计时钟。

    size_t path_len = strlen(path);
    size_t tmp_cap = path_len + 64U + 1U;
    char tmp_path[tmp_cap];

    int written = snprintf(tmp_path, tmp_cap, "%s.tmp-%" PRIu64 "-%" PRIu64, path, wall_timestamp_ms, pid);
    if (written < 0 || (size_t)written >= tmp_cap) {
        return ERR_UNKNOWN;
    }

    FILE* file_ptr = fopen(tmp_path, "wb");
    if (!file_ptr) {
        return ERR_FILE_OPEN;
    }
    if (fwrite(data, 1, len, file_ptr) != len) {
        (void)fclose(file_ptr);
        (void)remove(tmp_path);
        return ERR_FILE_WRITE;
    }
    if (dominoFlushFile(file_ptr) != 0) {
        (void)fclose(file_ptr);
        (void)remove(tmp_path);
        return ERR_FILE_FLUSH;
    }
    (void)fclose(file_ptr);

    if (dominoRename(tmp_path, path) != 0) {
        (void)remove(tmp_path);
        return ERR_FILE_RENAME;
    }

    return CODE_OK;
}

DOMINO_CODE dominoPathSplitParentBase(const char* path, char* parent, size_t parent_cap, char* base, size_t base_cap) {
    if (!path || !parent || !base || parent_cap == 0U || base_cap == 0U) {
        return ERR_INVALID_PARAM;
    }

    size_t len = strlen(path);
    if (len == 0U) {
        return ERR_INVALID_PARAM;
    }

    while (len > 0U && (path[len - 1U] == '/' || path[len - 1U] == '\\')) {
        len--;
    }
    if (len == 0U) {
        return ERR_INVALID_PARAM;
    }

    size_t sep_index = len;
    for (size_t i = len; i > 0U; i--) {
        char chr = path[i - 1U];
        if (chr == '/' || chr == '\\') {
            sep_index = i - 1U;
            break;
        }
    }

    int parent_written;
    int base_written;
    if (sep_index == len) {
        parent_written = snprintf(parent, parent_cap, ".");
        base_written = snprintf(base, base_cap, "%.*s", (int)len, path);
    } else if (sep_index == 0U && path[0] == '/') {
        parent_written = snprintf(parent, parent_cap, "/");
        base_written = snprintf(base, base_cap, "%.*s", (int)(len - 1U), path + 1U);
    } else {
        parent_written = snprintf(parent, parent_cap, "%.*s", (int)sep_index, path);
        base_written = snprintf(base, base_cap, "%.*s", (int)(len - sep_index - 1U), path + sep_index + 1U);
    }

    if (parent_written < 0 || (size_t)parent_written >= parent_cap || base_written < 0 || (size_t)base_written >= base_cap) {
        return ERR_INVALID_PARAM;
    }
    return CODE_OK;
}

int dominoMkdir(const char* path) {
    return mkdir(path, 0755);
}

DOMINO_CODE dominoMkdirRecursive(const char* path) {
    if (!path || path[0] == '\0') {
        return ERR_INVALID_PARAM;
    }

    char path_buffer[4096];
    int written = snprintf(path_buffer, sizeof(path_buffer), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(path_buffer)) {
        return ERR_OUT_OF_RANGE;
    }

    size_t path_len = (size_t)written;
    while (path_len > 1U && path_buffer[path_len - 1U] == '/') {
        path_buffer[--path_len] = '\0';
    }

    for (size_t char_index = 1U; char_index <= path_len; char_index++) {
        if (char_index < path_len && path_buffer[char_index] != '/') {
            continue;
        }
        char saved_char = path_buffer[char_index];
        path_buffer[char_index] = '\0';
        if (path_buffer[0] != '\0' && mkdir(path_buffer, 0755) != 0 && errno != EEXIST) {
            path_buffer[char_index] = saved_char;
            return ERR_FILE_WRITE;
        }
        struct stat path_stat;
        if (stat(path_buffer, &path_stat) != 0 || !S_ISDIR(path_stat.st_mode)) {
            path_buffer[char_index] = saved_char;
            return ERR_FILE_WRITE;
        }
        path_buffer[char_index] = saved_char;
    }
    return CODE_OK;
}

uint64_t dominoWallTimeMs(void) {
    struct timespec wall_time;
    if (timespec_get(&wall_time, TIME_UTC) != 0) {
        return ((uint64_t)wall_time.tv_sec * 1000ULL) + ((uint64_t)wall_time.tv_nsec / 1000000ULL);
    }
    return ((uint64_t)time(nullptr)) * 1000ULL;
}

uint64_t dominoMonotonicTimeNs(void) {
    struct timespec time_spec = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &time_spec) != 0) {
        return 0;
    }
    return ((uint64_t)time_spec.tv_sec * 1000000000ULL) + (uint64_t)time_spec.tv_nsec;
}

__attribute__((hot)) void dominoFormatUtcTimestampNow(char* out_ptr, size_t out_len) {
    if ((out_ptr == nullptr) || (out_len == 0U)) {
        return;
    }
    out_ptr[0] = '\0';

    struct timespec time_spec = {0};
    if (clock_gettime(CLOCK_REALTIME, &time_spec) != 0) {
        (void)snprintf(out_ptr, out_len, "0000-00-00T00:00:00.000Z");
        return;
    }

    time_t sec = (time_t)time_spec.tv_sec;
    struct tm tm_utc;
    if (gmtime_r(&sec, &tm_utc) == nullptr) {
        (void)snprintf(out_ptr, out_len, "0000-00-00T00:00:00.000Z");
        return;
    }

    char base[32] = {0};  // "YYYY-MM-DDTHH:MM:SS"
    if (strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm_utc) == 0U) {
        (void)snprintf(out_ptr, out_len, "0000-00-00T00:00:00.000Z");
        return;
    }

    long millis = time_spec.tv_nsec / 1000000L;
    (void)snprintf(out_ptr, out_len, "%s.%03ldZ", base, millis);
}

uint8_t* dominoReadFileBytes(const char* path, size_t* length, DOMINO_CODE* result) {
    if (!path || !length) {
        *result = ERR_INVALID_PARAM;
        return nullptr;
    }
    *length = 0;
    *result = CODE_OK;
    FILE* file_ptr = fopen(path, "rb");
    if (!file_ptr) {
        *result = ERR_FILE_OPEN;
        return nullptr;
    }
    if (fseek(file_ptr, 0, SEEK_END) != 0) {
        (void)fclose(file_ptr);
        *result = ERR_FILE_READ;
        return nullptr;
    }
    long size = ftell(file_ptr);
    if (size < 0) {
        (void)fclose(file_ptr);
        *result = ERR_FILE_READ;
        return nullptr;
    }
    if (size == 0) {
        (void)fclose(file_ptr);
        *result = CODE_OK;
        return nullptr;
    }

    if (fseek(file_ptr, 0, SEEK_SET) != 0) {
        (void)fclose(file_ptr);
        *result = ERR_FILE_READ;
        return nullptr;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)size);
    if (!buf) {
        (void)fclose(file_ptr);
        dominoAssertErrorCode(ERR_MEMORY_ALLOC, true, __FILE__, __LINE__);
    }
    size_t read_n = fread(buf, 1, (size_t)size, file_ptr);
    (void)fclose(file_ptr);
    /* 短读视为失败，避免返回截断内容 */
    if (read_n != (size_t)size) {
        free(buf);
        *result = ERR_FILE_READ;
        return nullptr;
    }
    *length = (size_t)size;
    *result = CODE_OK;
    return buf;
}

int dominoRemovePathRecursive(const char* path) {
    typedef struct DominoRemovePathNode {
        char path[4096];
        uint8_t children_done_flag;
    } DominoRemovePathNode;

    size_t stack_cap = 16U;
    size_t stack_len = 1U;
    int result = 0;
    DominoRemovePathNode* stack_ptr = (DominoRemovePathNode*)malloc(stack_cap * sizeof(*stack_ptr));
    if (!stack_ptr) {
        return -1;
    }

    int written = snprintf(stack_ptr[0].path, sizeof(stack_ptr[0].path), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(stack_ptr[0].path)) {
        free(stack_ptr);
        return -1;
    }
    stack_ptr[0].children_done_flag = 0U;

    while (stack_len > 0U) {
        size_t node_index = stack_len - 1U;
        DominoRemovePathNode* node_ptr = &stack_ptr[node_index];

        struct stat _stat;
        if (lstat(node_ptr->path, &_stat) != 0) {
            if (errno == ENOENT) {
                stack_len--;
                continue;
            }
            result = -1;
            break;
        }

        if (!S_ISDIR(_stat.st_mode)) {
            if (unlink(node_ptr->path) != 0) {
                result = -1;
                break;
            }
            stack_len--;
            continue;
        }

        if (node_ptr->children_done_flag != 0U) {
            if (rmdir(node_ptr->path) != 0) {
                result = -1;
                break;
            }
            stack_len--;
            continue;
        }

        node_ptr->children_done_flag = 1U;

        DIR* dir_ptr = opendir(node_ptr->path);
        if (!dir_ptr) {
            result = -1;
            break;
        }

        struct dirent* entry_ptr;
        while ((entry_ptr = readdir(dir_ptr)) != nullptr) {
            if (strcmp(entry_ptr->d_name, ".") == 0 || strcmp(entry_ptr->d_name, "..") == 0) {
                continue;
            }

            if (stack_len == stack_cap) {
                if (stack_cap > SIZE_MAX / sizeof(*stack_ptr) / 2U) {
                    (void)closedir(dir_ptr);
                    result = -1;
                    goto cleanup;
                }
                size_t new_stack_cap = stack_cap * 2U;
                DominoRemovePathNode* new_stack_ptr = (DominoRemovePathNode*)realloc(stack_ptr, new_stack_cap * sizeof(*stack_ptr));
                if (!new_stack_ptr) {
                    (void)closedir(dir_ptr);
                    result = -1;
                    goto cleanup;
                }
                stack_ptr = new_stack_ptr;
                stack_cap = new_stack_cap;
                node_ptr = &stack_ptr[node_index];
            }

            written = snprintf(stack_ptr[stack_len].path, sizeof(stack_ptr[stack_len].path), "%s/%s", node_ptr->path, entry_ptr->d_name);
            if (written < 0 || (size_t)written >= sizeof(stack_ptr[stack_len].path)) {
                (void)closedir(dir_ptr);
                result = -1;
                goto cleanup;
            }
            stack_ptr[stack_len].children_done_flag = 0U;
            stack_len++;
        }
        (void)closedir(dir_ptr);
    }

cleanup:
    free(stack_ptr);
    return result;
}

int dominoPathExists(const char* path, bool directory_only) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    struct stat _stat;
    if (stat(path, &_stat) != 0) {
        return 0;
    }
    if (directory_only) {
        return S_ISDIR(_stat.st_mode) ? 1 : 0;
    }
    return (S_ISREG(_stat.st_mode) || S_ISDIR(_stat.st_mode)) ? 1 : 0;
}
