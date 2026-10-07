#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "../queue/thread_queue.h"
#include "domino_engine_log.h"
#include "domino_shared_common.h"
#include "domino_shared_error_codes.h"

#define DOMINO_LOG_DEFAULT_MIN_LEVEL DOMINO_LOG_LEVEL_INFO
#define DOMINO_LOG_DEFAULT_QUEUE_CAPACITY 10240U

#define DOMINO_LOG_TRUNC_SUFFIX "...(TRUNCATED)"
#define DOMINO_LOG_TRUNC_SUFFIX_LEN (sizeof(DOMINO_LOG_TRUNC_SUFFIX) - 1U)

#define DOMINO_LOG_WRITER_BATCH_MAX 128U

/*包含结尾'\0'*/
#define DOMINO_LOG_MESSAGE_MAX 256U
#define DOMINO_LOG_SOURCE_MAX 128U
#define DOMINO_LOG_SOURCE_FILE_CAPTURE_MAX 80U
#define DOMINO_LOG_SOURCE_FUNC_CAPTURE_MAX 32U

static_assert(DOMINO_LOG_MESSAGE_MAX > (DOMINO_LOG_TRUNC_SUFFIX_LEN + 2U), "DOMINO_LOG_MESSAGE_MAX too small for trunc suffix");
static_assert(DOMINO_LOG_SOURCE_FILE_CAPTURE_MAX + DOMINO_LOG_SOURCE_FUNC_CAPTURE_MAX + 16U <= DOMINO_LOG_SOURCE_MAX,
              "DOMINO_LOG_SOURCE_MAX is too small for bounded source components");
static_assert(DOMINO_LOG_DEFAULT_QUEUE_CAPACITY > 1U, "default queue capacity must be positive");
static_assert(DOMINO_LOG_WRITER_BATCH_MAX > 1U, "writer batch max must be positive");
static_assert(DOMINO_LOG_MAX_MODULES > 1U, "max modules must be positive");

inline static bool dominoLogLevelIsValid(DOMINO_LOG_LEVEL level) {
    return (level >= DOMINO_LOG_LEVEL_TRACE && level <= DOMINO_LOG_LEVEL_ERROR) != 0;
}

inline static const char* dominoLogLevelName(DOMINO_LOG_LEVEL level) {
    switch (level) {
        case DOMINO_LOG_LEVEL_TRACE:
            return "TRACE";
        case DOMINO_LOG_LEVEL_DEBUG:
            return "DEBUG";
        case DOMINO_LOG_LEVEL_INFO:
            return "INFO";
        case DOMINO_LOG_LEVEL_WARN:
            return "WARN";
        case DOMINO_LOG_LEVEL_ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

typedef struct DominoLogQueueItem {
    uint64_t id;      // 队列内部维护，与日志序号 seq 独立。
    size_t body_len;  // 不包含结尾'\0'
    uint32_t seq;
    uint32_t tid;
    DOMINO_LOG_LEVEL level;
    domino_log_module_id_t module_id;
    bool source_truncated;
    char source[DOMINO_LOG_SOURCE_MAX];
    char body[DOMINO_LOG_MESSAGE_MAX];  // 包含结尾'\0'
} DominoLogQueueItem;

static_assert(offsetof(DominoLogQueueItem, id) == 0U, "queue ID must be the first log item field");

/* 线程编号在进程内共享，同一生产线程写不同实例时保持相同 tid。 */
static _Atomic uint32_t g_tid_next = 0U;
static thread_local uint32_t g_tid_cached = 0U;

inline static void dominoThrdAssertSuccess(int result_code, const char* what_ptr) {
    if (result_code == thrd_success) {
        return;
    }
    const char* what_str = (what_ptr != nullptr) ? what_ptr : "thread-api";
    (void)fprintf(stderr, "domino log: %s failed (rc=%d)\n", what_str, result_code);
    abort();
}

static inline void dominoLogQueueAssertSuccess(DOMINO_CODE result_code, const char* what_str) {
    if (result_code == CODE_OK) {
        return;
    }
    (void)fprintf(stderr, "domino log: queue %s failed (rc=%d)\n", what_str, (int)result_code);
    abort();
}

static uint32_t dominoGetNumericThreadId(void) {
    // 后续改造为跨平台，系统线程ID
    if (g_tid_cached != 0U) {
        return g_tid_cached;
    }
    uint32_t thread_id = atomic_fetch_add(&g_tid_next, 1U) + 1U;
    assert(thread_id != 0U);
    g_tid_cached = thread_id;
    return thread_id;
}

inline static size_t dominoLogBoundedStringLength(const char* value_ptr, size_t max_len, bool* truncated_out_ptr) {
    size_t value_len = 0U;
    while (value_len < max_len && value_ptr[value_len] != '\0') {
        value_len++;
    }
    *truncated_out_ptr = value_ptr[value_len] != '\0';
    return value_len;
}

static void dominoLogSetSourceLocation(DominoLogQueueItem* item_ptr, DominoLogSourceLocation loc) {
    const char* file_ptr = loc.file != nullptr ? loc.file : "?";
    const char* func_ptr = loc.func != nullptr ? loc.func : "?";
    bool file_truncated = false;
    bool func_truncated = false;
    size_t file_len = dominoLogBoundedStringLength(file_ptr, DOMINO_LOG_SOURCE_FILE_CAPTURE_MAX, &file_truncated);
    size_t func_len = dominoLogBoundedStringLength(func_ptr, DOMINO_LOG_SOURCE_FUNC_CAPTURE_MAX, &func_truncated);
    int line_num = loc.line > 0 ? loc.line : 0;

    int source_result =
        snprintf(item_ptr->source, sizeof(item_ptr->source), "%.*s:%d:%.*s", (int)file_len, file_ptr, line_num, (int)func_len, func_ptr);
    if (source_result < 0) {
        (void)snprintf(item_ptr->source, sizeof(item_ptr->source), "%s", "?:0:?");
        item_ptr->source_truncated = true;
        return;
    }
    item_ptr->source_truncated = ((file_truncated || func_truncated || (size_t)source_result >= sizeof(item_ptr->source)) != 0);
}

inline static void dominoLogInitQueueItem(DominoLogger* logger_ptr, DominoLogQueueItem* item_ptr, domino_log_module_id_t module_id,
                                          DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc) {
    item_ptr->module_id = module_id;
    item_ptr->seq = atomic_fetch_add(&logger_ptr->seq, 1U) + 1U;
    item_ptr->tid = dominoGetNumericThreadId();
    item_ptr->level = level;
    dominoLogSetSourceLocation(item_ptr, loc);
}

static size_t dominoLogBuildOutputLine(const DominoLogQueueItem* item_ptr, const char* ts_ptr, const char* module_name, char* line_out_ptr,
                                       size_t line_out_size) {
    if (!item_ptr || !ts_ptr || !module_name || !line_out_ptr || line_out_size <= (DOMINO_LOG_TRUNC_SUFFIX_LEN + 2U)) {
        return 0U;
    }

    const size_t content_capacity = line_out_size - 2U;  // 为换行和结尾 NUL 预留空间。
    const char* level_name = dominoLogLevelName(item_ptr->level);
    int prefix_result = snprintf(line_out_ptr, line_out_size, "[%s][tid=%" PRIu32 "][seq=%" PRIu32 "][%s][%s][%s] ", ts_ptr, item_ptr->tid,
                                 item_ptr->seq, level_name, module_name, item_ptr->source);

    bool truncated = item_ptr->source_truncated;
    size_t used_len = 0U;
    if (prefix_result < 0) {
        static const char prefix_error[] = "[log-prefix-error] ";
        used_len = sizeof(prefix_error) - 1U;
        if (used_len > content_capacity) {
            used_len = content_capacity;
        }
        memcpy(line_out_ptr, prefix_error, used_len);
        truncated = true;
    } else if ((size_t)prefix_result > content_capacity) {
        used_len = content_capacity;
        truncated = true;
    } else {
        used_len = (size_t)prefix_result;
    }

    size_t remaining_len = content_capacity - used_len;
    size_t body_copy_len = item_ptr->body_len < remaining_len ? item_ptr->body_len : remaining_len;
    if (body_copy_len > 0U) {
        memcpy(line_out_ptr + used_len, item_ptr->body, body_copy_len);
    }
    used_len += body_copy_len;
    if (body_copy_len < item_ptr->body_len) {
        truncated = true;
    }

    if (truncated) {
        size_t max_content_len = content_capacity - DOMINO_LOG_TRUNC_SUFFIX_LEN;
        used_len = used_len < max_content_len ? used_len : max_content_len;
        memcpy(line_out_ptr + used_len, DOMINO_LOG_TRUNC_SUFFIX, DOMINO_LOG_TRUNC_SUFFIX_LEN);
        used_len += DOMINO_LOG_TRUNC_SUFFIX_LEN;
    }

    line_out_ptr[used_len++] = '\n';
    line_out_ptr[used_len] = '\0';
    return used_len;
}

__attribute__((hot)) static void dominoLogWriteOneWithTimestamp(const DominoLogger* logger_ptr, const DominoLogQueueItem* item_ptr,
                                                                const char* ts_ptr) {
    const char* module_name = "unknown";
    for (size_t i = 0U; i < logger_ptr->module_count; i++) {
        if (logger_ptr->modules[i].module_id == item_ptr->module_id) {
            module_name = logger_ptr->modules[i].name;
            break;
        }
    }

    char line_buf[DOMINO_LOG_MESSAGE_MAX] = {0};
    size_t out_len = dominoLogBuildOutputLine(item_ptr, ts_ptr, module_name, line_buf, sizeof(line_buf));
    if (out_len == 0U) {
        return;
    }

    FILE* out_ptr = (item_ptr->level >= DOMINO_LOG_LEVEL_ERROR) ? stderr : stdout;
    (void)fwrite(line_buf, 1, out_len, out_ptr);

    if (logger_ptr->file_ptr != nullptr) {
        (void)fwrite(line_buf, 1, out_len, logger_ptr->file_ptr);
    }
}

static int dominoLogWriterThread(void* arg_ptr) {
    DominoLogger* logger_ptr = arg_ptr;
    DominoLogQueueItem items[DOMINO_LOG_WRITER_BATCH_MAX];

    while (true) {
        uint32_t batch_count;
        DOMINO_CODE result_code = dominoThreadQueueConsumeBatch(logger_ptr->queue_ptr, items, DOMINO_LOG_WRITER_BATCH_MAX, &batch_count, true);
        if (result_code == ERR_QUEUE_PRODUCER_STOPPED) {
            break;
        }
        dominoLogQueueAssertSuccess(result_code, "consume batch");

        char ts_buf[32] = {0};
        dominoFormatUtcTimestampNow(ts_buf, sizeof(ts_buf));
        for (size_t i = 0U; i < batch_count; i++) {
            dominoLogWriteOneWithTimestamp(logger_ptr, &items[i], ts_buf);
        }
    }

    (void)fflush(stdout);
    (void)fflush(stderr);
    if (logger_ptr->file_ptr != nullptr) {
        (void)fflush(logger_ptr->file_ptr);
    }
    return 0;
}

static int dominoLogValidateModules(const DominoLogModule* modules_ptr, size_t module_count) {
    if (module_count > (size_t)DOMINO_LOG_MAX_MODULES) {
        return ERR_OUT_OF_RANGE;
    }
    if (module_count > 0U && modules_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    for (size_t module_index = 0U; module_index < module_count; module_index++) {
        const DominoLogModule* module_ptr = &modules_ptr[module_index];
        if (module_ptr->name[0] == '\0' || memchr(module_ptr->name, '\0', sizeof(module_ptr->name)) == nullptr) {
            return ERR_INVALID_PARAM;
        }
        if (module_ptr->min_level != 0 && !dominoLogLevelIsValid(module_ptr->min_level)) {
            return ERR_INVALID_PARAM;
        }
        for (size_t previous_index = 0U; previous_index < module_index; previous_index++) {
            if (modules_ptr[previous_index].module_id == module_ptr->module_id || strcmp(modules_ptr[previous_index].name, module_ptr->name) == 0) {
                return ERR_ALREADY_EXISTS;
            }
        }
    }
    return CODE_OK;
}

int dominoLogInit(DominoLogConfig config, const DominoLogModule* modules_ptr, size_t module_count, DominoLogger* logger_ptr) {
    if (logger_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    memset(logger_ptr, 0, sizeof(*logger_ptr));
    atomic_init(&logger_ptr->seq, 0U);
    if (config.min_level != 0 && !dominoLogLevelIsValid(config.min_level)) {
        return ERR_INVALID_PARAM;
    }
    int result = dominoLogValidateModules(modules_ptr, module_count);
    if (result != CODE_OK) {
        return result;
    }
    size_t capacity = config.buffer_size == 0U ? (size_t)DOMINO_LOG_DEFAULT_QUEUE_CAPACITY : config.buffer_size;
    if (capacity > SIZE_MAX / sizeof(DominoLogQueueItem)) {
        return ERR_MEMORY_OVERFLOW;
    }
    if (capacity > UINT32_MAX) {
        return ERR_OUT_OF_RANGE;
    }

    logger_ptr->default_min_level = config.min_level == 0 ? DOMINO_LOG_DEFAULT_MIN_LEVEL : config.min_level;
    for (size_t module_index = 0U; module_index < module_count; module_index++) {
        DominoLogModule module = modules_ptr[module_index];
        module.min_level = module.min_level == 0 ? logger_ptr->default_min_level : module.min_level;
        logger_ptr->modules[module_index] = module;
    }
    logger_ptr->module_count = module_count;

    result = ERR_MEMORY_ALLOC;
    logger_ptr->queue_ptr = calloc(1U, sizeof(*logger_ptr->queue_ptr));
    if (logger_ptr->queue_ptr == nullptr) {
        goto fail_init;
    }
    result = dominoThreadQueueInit(logger_ptr->queue_ptr, sizeof(DominoLogQueueItem), (uint32_t)capacity, (uint32_t)capacity);
    if (result != CODE_OK) {
        goto fail_queue;
    }

    result = ERR_FILE_OPEN;
    if (config.file_path != nullptr && config.file_path[0] != '\0') {
        logger_ptr->file_ptr = fopen(config.file_path, "ab");
        if (logger_ptr->file_ptr == nullptr) {
            goto fail_queue;
        }
    }
    result = ERR_THREAD_CREATE;
    if (thrd_create(&logger_ptr->writer_thread, dominoLogWriterThread, logger_ptr) != thrd_success) {
        goto fail_file;
    }

    return CODE_OK;

fail_file:
    if (logger_ptr->file_ptr != nullptr) {
        (void)fclose(logger_ptr->file_ptr);
    }
fail_queue:
    dominoThreadQueueDestroy(logger_ptr->queue_ptr);
    free(logger_ptr->queue_ptr);
fail_init:
    memset(logger_ptr, 0, sizeof(*logger_ptr));
    atomic_init(&logger_ptr->seq, 0U);
    return result;
}

void dominoLogDestroy(DominoLogger* logger_ptr) {
    if (logger_ptr == nullptr || logger_ptr->queue_ptr == nullptr) {
        return;
    }

    dominoThreadQueueStop(logger_ptr->queue_ptr, DOMINO_THREAD_QUEUE_PRODUCER);
    dominoThreadQueueWakeAll(logger_ptr->queue_ptr);

    dominoThrdAssertSuccess(thrd_join(logger_ptr->writer_thread, nullptr), "thrd_join(writer)");

    if (logger_ptr->file_ptr != nullptr) {
        (void)fclose(logger_ptr->file_ptr);
    }

    dominoThreadQueueDestroy(logger_ptr->queue_ptr);
    free(logger_ptr->queue_ptr);

    memset(logger_ptr, 0, sizeof(*logger_ptr));
    atomic_init(&logger_ptr->seq, 0U);
}

static bool dominoLogShouldEmit(const DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level) {
    if (logger_ptr == nullptr || !dominoLogLevelIsValid(level)) {
        return false;
    }
    for (size_t i = 0U; i < logger_ptr->module_count; i++) {
        if (logger_ptr->modules[i].module_id == module_id) {
            return (level >= logger_ptr->modules[i].min_level);
        }
    }
    return false;
}

void dominoLogVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt,
                 va_list arg_list) {
    if ((fmt == nullptr) || (!dominoLogShouldEmit(logger_ptr, module_id, level))) {
        return;
    }

    DominoLogQueueItem item = {0};
    dominoLogInitQueueItem(logger_ptr, &item, module_id, level, loc);

    va_list arg_copy;
    va_copy(arg_copy, arg_list);
    int formatted_len = vsnprintf(item.body, (size_t)DOMINO_LOG_MESSAGE_MAX, fmt, arg_copy);
    va_end(arg_copy);
    if (formatted_len <= 0) {
        return;
    }

    item.body_len = (size_t)formatted_len < DOMINO_LOG_MESSAGE_MAX ? (size_t)formatted_len : DOMINO_LOG_MESSAGE_MAX - 1U;
    dominoLogQueueAssertSuccess(dominoThreadQueueProduce(logger_ptr->queue_ptr, &item, true), "produce");
}

void dominoLog(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt,
               ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    dominoLogVa(logger_ptr, module_id, level, loc, fmt, arg_list);
    va_end(arg_list);
}

void dominoLogMsg(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc,
                  const char* msg_ptr) {
    if (logger_ptr == nullptr || msg_ptr == nullptr) {
        return;
    }
    dominoLogMsgN(logger_ptr, module_id, level, loc, msg_ptr, strlen(msg_ptr));
}

void dominoLogMsgN(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc,
                   const char* msg_ptr, size_t msg_len) {
    if ((msg_ptr == nullptr) || (!dominoLogShouldEmit(logger_ptr, module_id, level)) || (msg_len == 0U)) {
        return;
    }

    DominoLogQueueItem item = {0};
    dominoLogInitQueueItem(logger_ptr, &item, module_id, level, loc);

    size_t copy_len = msg_len;
    if (copy_len >= (size_t)DOMINO_LOG_MESSAGE_MAX) {
        copy_len = (size_t)DOMINO_LOG_MESSAGE_MAX - 1U;
    }
    if (copy_len != 0U) {
        memcpy(item.body, msg_ptr, copy_len);
    }
    item.body[copy_len] = '\0';
    item.body_len = copy_len;

    dominoLogQueueAssertSuccess(dominoThreadQueueProduce(logger_ptr->queue_ptr, &item, true), "produce");
}
