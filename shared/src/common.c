#define _XOPEN_SOURCE 700

#include <errno.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "domino_shared_common.h"
#include "domino_shared_error_codes.h"

[[noreturn]] void dominoExitWithErrorAt(DOMINO_CODE code, const char* file_str, int line, const char* function_str, const char* fmt, ...) {
    (void)fprintf(stderr, "[ERROR][%s:%d:%s] ", file_str, line, function_str);
    va_list arg_list;
    va_start(arg_list, fmt);
    (void)vfprintf(stderr, fmt, arg_list);
    va_end(arg_list);
    (void)fprintf(stderr, " (%s, code=%d)\n", dominoErrorCodeToString(code), (int)code);
    exit(EXIT_FAILURE);
}

void dominoPrintMessageAt(const char* file_str, int line, const char* function_str, const char* fmt, ...) {
    if (fmt == nullptr) {
        return;
    }
    (void)fprintf(stdout, "[%s:%d:%s] ", file_str != nullptr ? file_str : "?", line > 0 ? line : 0, function_str != nullptr ? function_str : "?");
    va_list arg_list;
    va_start(arg_list, fmt);
    (void)vfprintf(stdout, fmt, arg_list);
    va_end(arg_list);
    (void)fputc('\n', stdout);
    (void)fflush(stdout);
}

uint64_t dominoGetPid(void) {
    return (uint64_t)getpid();
}

FILE* dominoOpenAppendFile(const char* file_path) {
    if (file_path == nullptr || file_path[0] == '\0') {
        return nullptr;
    }
    FILE* file_ptr = fopen(file_path, "ab");
    if (file_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_OPEN, "file open failed: %s", file_path);
    }
    return file_ptr;
}

void dominoFlushAndCloseFile(FILE* file_ptr) {
    if (file_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "file close requires an open output file");
    }
    if (fflush(file_ptr) != 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_FLUSH, "file flush failed");
    }
    if (fclose(file_ptr) != 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_CLOSE, "file close failed");
    }
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
    if (!path || !data) {
        return ERR_NULL_POINTER;
    }
    if (path[0] == '\0' || len == 0U) {
        return ERR_INVALID_PARAM;
    }
    const char tmp_suffix[] = ".tmp-XXXXXX";
    size_t path_len = strlen(path);
    if (path_len > SIZE_MAX - sizeof(tmp_suffix)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "atomic file temporary path size overflows size_t");
    }
    size_t tmp_cap = path_len + sizeof(tmp_suffix);
    char tmp_path[tmp_cap];
    memcpy(tmp_path, path, path_len);
    memcpy(tmp_path + path_len, tmp_suffix, sizeof(tmp_suffix));

    int file_fd = mkstemp(tmp_path);
    if (file_fd < 0) {
        return ERR_FILE_OPEN;
    }
    FILE* file_ptr = fdopen(file_fd, "wb");
    if (!file_ptr) {
        (void)close(file_fd);
        (void)remove(tmp_path);
        return ERR_FILE_OPEN;
    }
    DOMINO_CODE code = CODE_OK;
    if (fwrite(data, 1, len, file_ptr) != len) {
        code = ERR_FILE_WRITE;
        goto close_file;
    }
    if (dominoFlushFile(file_ptr) != 0) {
        code = ERR_FILE_FLUSH;
        goto close_file;
    }
close_file:
    if (fclose(file_ptr) != 0 && code == CODE_OK) {
        code = ERR_FILE_CLOSE;
    }
    if (code == CODE_OK && dominoRename(tmp_path, path) != 0) {
        code = ERR_FILE_RENAME;
    }
    if (code != CODE_OK) {
        (void)remove(tmp_path);
    }
    return code;
}

DOMINO_CODE dominoPathSplitParentBase(const char* path, char* parent, size_t parent_cap, char* base, size_t base_cap) {
    if (!path || !parent || !base) {
        return ERR_NULL_POINTER;
    }

    size_t len = strlen(path);
    while (len > 0U && path[len - 1U] == '/') {
        len--;
    }
    if (len == 0U) {
        return ERR_INVALID_PARAM;
    }

    size_t sep_index = len;
    for (size_t i = len; i > 0U; i--) {
        char chr = path[i - 1U];
        if (chr == '/') {
            sep_index = i - 1U;
            break;
        }
    }

    const char* parent_source = path;
    size_t parent_len;
    size_t base_offset;
    if (sep_index == len) {
        parent_source = ".";
        parent_len = 1U;
        base_offset = 0U;
    } else if (sep_index == 0U) {
        parent_len = 1U;
        base_offset = 1U;
    } else {
        parent_len = sep_index;
        base_offset = sep_index + 1U;
    }

    size_t base_len = len - base_offset;
    if (parent_len >= parent_cap || base_len >= base_cap) {
        return ERR_BUFFER_TOO_SMALL;
    }
    memcpy(parent, parent_source, parent_len);
    parent[parent_len] = '\0';
    memcpy(base, path + base_offset, base_len);
    base[base_len] = '\0';
    return CODE_OK;
}

int dominoMkdir(const char* path) {
    return mkdir(path, 0755);
}

DOMINO_CODE dominoMkdirRecursive(const char* path) {
    if (!path) {
        return ERR_NULL_POINTER;
    }
    if (path[0] == '\0') {
        return ERR_INVALID_PARAM;
    }

    char path_buffer[4096];
    size_t path_len = strlen(path);
    if (path_len >= sizeof(path_buffer)) {
        return ERR_BUFFER_TOO_SMALL;
    }
    memcpy(path_buffer, path, path_len + 1U);

    while (path_len > 1U && path_buffer[path_len - 1U] == '/') {
        path_buffer[--path_len] = '\0';
    }

    for (size_t char_index = 1U; char_index <= path_len; char_index++) {
        if (char_index < path_len && path_buffer[char_index] != '/') {
            continue;
        }
        char saved_char = path_buffer[char_index];
        path_buffer[char_index] = '\0';
        // 非空路径且 char_index >= 1，当前前缀一定非空。
        if (mkdir(path_buffer, 0755) != 0 && errno != EEXIST) {
            return ERR_DIRECTORY_CREATE;
        }
        struct stat path_stat;
        if (stat(path_buffer, &path_stat) != 0) {
            return ERR_FILE_STAT;
        }
        if (!S_ISDIR(path_stat.st_mode)) {
            return ERR_PATH_TYPE;
        }
        path_buffer[char_index] = saved_char;
    }
    return CODE_OK;
}

uint64_t dominoWallTimeMs(void) {
    struct timespec wall_time;
    if (timespec_get(&wall_time, TIME_UTC) != TIME_UTC) {
        DOMINO_EXIT_WITH_ERROR(ERR_CLOCK_READ, "UTC clock sampling failed");
    }
    return ((uint64_t)wall_time.tv_sec * 1000ULL) + ((uint64_t)wall_time.tv_nsec / 1000000ULL);
}

uint64_t dominoMonotonicTimeNs(void) {
    struct timespec time_spec;
    if (timespec_get(&time_spec, TIME_MONOTONIC) != TIME_MONOTONIC) {
        DOMINO_EXIT_WITH_ERROR(ERR_CLOCK_READ, "monotonic clock sampling failed");
    }
    return ((uint64_t)time_spec.tv_sec * 1000000000ULL) + (uint64_t)time_spec.tv_nsec;
}

__attribute__((hot)) DOMINO_CODE dominoFormatUtcTimestampNow(char* out_ptr, size_t out_len) {
    if (out_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (out_len == 0U) {
        return ERR_BUFFER_TOO_SMALL;
    }
    out_ptr[0] = '\0';

    struct timespec time_spec;
    if (timespec_get(&time_spec, TIME_UTC) != TIME_UTC) {
        DOMINO_EXIT_WITH_ERROR(ERR_CLOCK_READ, "log UTC clock sampling failed");
    }

    time_t sec = (time_t)time_spec.tv_sec;
    struct tm tm_utc;
    if (gmtime_r(&sec, &tm_utc) == nullptr) {
        return ERR_STRING_FORMAT;
    }

    // tm_year 从 1900 年起计数；加法前限定年份为 0000～9999。
    if (tm_utc.tm_year < -1900 || tm_utc.tm_year > 8099) {
        return ERR_OUT_OF_RANGE;
    }

    long millisecond_ms = time_spec.tv_nsec / 1000000L;
    int written = snprintf(out_ptr, out_len, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                           tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, millisecond_ms);
    if (written < 0) {
        out_ptr[0] = '\0';
        return ERR_STRING_FORMAT;
    }
    if ((size_t)written >= out_len) {
        out_ptr[0] = '\0';
        return ERR_BUFFER_TOO_SMALL;
    }
    return CODE_OK;
}

DOMINO_CODE dominoReadFileBytes(const char* path, uint8_t** data_ptr, size_t* length_ptr) {
    if (!data_ptr || !length_ptr) {
        return ERR_NULL_POINTER;
    }
    *data_ptr = nullptr;
    *length_ptr = 0;
    if (!path) {
        return ERR_NULL_POINTER;
    }
    if (path[0] == '\0') {
        return ERR_INVALID_PARAM;
    }

    FILE* file_ptr = fopen(path, "rb");
    if (!file_ptr) {
        return ERR_FILE_OPEN;
    }
    DOMINO_CODE code = CODE_OK;
    uint8_t* file_buf = nullptr;
    if (fseek(file_ptr, 0, SEEK_END) != 0) {
        code = ERR_FILE_READ;
        goto close_file;
    }
    long file_size = ftell(file_ptr);
    if (file_size < 0) {
        code = ERR_FILE_READ;
        goto close_file;
    }
    if (file_size == 0) {
        return fclose(file_ptr) == 0 ? CODE_OK : ERR_FILE_CLOSE;
    }

    if (fseek(file_ptr, 0, SEEK_SET) != 0) {
        code = ERR_FILE_READ;
        goto close_file;
    }
    file_buf = (uint8_t*)malloc((size_t)file_size);
    if (!file_buf) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "file read buffer allocation failed");
    }
    /* 短读视为失败，避免返回截断内容。 */
    if (fread(file_buf, 1, (size_t)file_size, file_ptr) != (size_t)file_size) {
        code = ERR_FILE_READ;
        goto close_file;
    }
close_file:
    if (fclose(file_ptr) != 0 && code == CODE_OK) {
        code = ERR_FILE_CLOSE;
    }
    if (code != CODE_OK) {
        free(file_buf);
        return code;
    }
    *data_ptr = file_buf;
    *length_ptr = (size_t)file_size;
    return CODE_OK;
}

/** @brief 按后序遍历删除当前路径；非零错误码使 nftw 立即停止。 */
static int removePathEntry(const char* path, const struct stat* stat_ptr, int type_flag, struct FTW* walk_ptr) {
    (void)stat_ptr;
    (void)walk_ptr;

    if (type_flag == FTW_NS) {
        return ERR_FILE_STAT;
    }
    if (type_flag == FTW_DNR) {
        return ERR_DIRECTORY_OPEN;
    }
    if (remove(path) != 0 && errno != ENOENT) {
        return ERR_FILE_REMOVE;
    }
    return CODE_OK;
}

/** @brief 删除有效路径；入口已检查非空并去除非根路径的末尾分隔符。 */
static DOMINO_CODE removePath(const char* path) {
    struct stat path_stat;
    if (lstat(path, &path_stat) != 0) {
        return errno == ENOENT ? CODE_OK : ERR_FILE_STAT;
    }
    // 复用 lstat 类型：非目录直接删除，也避免 nftw 跟随根路径上的目录符号链接。
    if (!S_ISDIR(path_stat.st_mode)) {
        return remove(path) == 0 || errno == ENOENT ? CODE_OK : ERR_FILE_REMOVE;
    }

    // 先删除子项，再删除目录；不跟随符号链接，最多同时打开 16 个目录。
    int result = nftw(path, removePathEntry, 16, FTW_DEPTH | FTW_PHYS);
    if (result == -1) {
        if (errno == ENOMEM) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "directory traversal allocation failed");
        }
        return ERR_DIRECTORY_READ;
    }
    // 回调只返回 CODE_OK 或小于 -1 的错误码，与 nftw 自身的失败区分。
    return (DOMINO_CODE)result;
}

DOMINO_CODE dominoRemovePathRecursive(const char* path) {
    if (!path) {
        return ERR_NULL_POINTER;
    }
    if (path[0] == '\0') {
        return ERR_INVALID_PARAM;
    }

    size_t path_len = strlen(path);
    while (path_len > 0U && path[path_len - 1U] == '/') {
        path_len--;
    }
    if (path_len == 0U || path[path_len] == '\0') {
        return removePath(path);
    }

    // 尾斜杠会让 lstat 解析目录链接的目标；先移除，保持只删除链接本身的约定。
    char normalized_path[path_len + 1U];
    memcpy(normalized_path, path, path_len);
    normalized_path[path_len] = '\0';
    return removePath(normalized_path);
}
