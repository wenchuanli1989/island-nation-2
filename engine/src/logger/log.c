#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "../entry.h"
#include "../queue/thread_queue.h"
#include "domino_engine_log.h"
#include "domino_shared_common.h"
#include "domino_shared_error_codes.h"
#include "error.h"

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
static_assert(DOMINO_LOG_MAX_MODULES <= UINT8_MAX + 1U, "log module index must fit in uint8_t");

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
    uint8_t module_index;  // 生产者完成过滤后保存的只读模块表下标。
    bool source_truncated;
    char source[DOMINO_LOG_SOURCE_MAX];
    char body[DOMINO_LOG_MESSAGE_MAX];  // 包含结尾'\0'
} DominoLogQueueItem;

static_assert(offsetof(DominoLogQueueItem, id) == 0U, "queue ID must be the first log item field");

/* 线程编号在进程内共享，同一生产线程写不同实例时保持相同 tid。 */
static _Atomic uint32_t g_tid_next = 0U;
static thread_local uint32_t g_tid_cached = 0U;

/** @brief 保存实例的首个生命周期错误；失败后不清理或重试。 */
static int dominoLogFailLifecycle(DominoLogger* logger_ptr, int result_code) {
    int expected_result = CODE_OK;
    atomic_compare_exchange_strong(&logger_ptr->lifecycle_result, &expected_result, result_code);
    return atomic_load(&logger_ptr->lifecycle_result);
}

/** @brief 日志运行失败直接上报首错并停止队列；不再写日志，保活资源直到宿主结束进程。 */
static DOMINO_CODE dominoLogFailRuntime(DominoLogger* logger_ptr, DOMINO_CODE result_code) {
    int expected_result = CODE_OK;
    if (!atomic_compare_exchange_strong(&logger_ptr->lifecycle_result, &expected_result, result_code)) {
        return (DOMINO_CODE)expected_result;
    }
    (void)dominoEngineReportFailure(result_code);
    const DOMINO_CODE stop_code = dominoThreadQueueStopAndWake(logger_ptr->queue_ptr, DOMINO_THREAD_QUEUE_BOTH);
    if (stop_code != CODE_OK) {
        (void)dominoEngineReportFailure(stop_code);
    }
    return result_code;
}

/** @brief 入队不等待空位；错误由日志实例保存并直接上报引擎，禁止通过日志通道报告日志自身故障。 */
static void dominoLogProduce(DominoLogger* logger_ptr, DominoLogQueueItem* item_ptr) {
    if (atomic_load(&logger_ptr->lifecycle_result) != CODE_OK) {
        return;
    }
    const DOMINO_CODE code = dominoThreadQueueProduce(logger_ptr->queue_ptr, item_ptr);
    if (code != CODE_OK) {
        (void)DOMINO_LOG_DIRECT_ERROR(code, "log message could not enter the writer queue");
        (void)dominoLogFailRuntime(logger_ptr, code);
    }
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

static DOMINO_CODE dominoLogSetSourceLocation(DominoLogQueueItem* item_ptr, DominoLogSourceLocation loc) {
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
        return DOMINO_LOG_DIRECT_ERROR(ERR_STRING_FORMAT, "log source location formatting failed");
    }
    item_ptr->source_truncated = ((file_truncated || func_truncated || (size_t)source_result >= sizeof(item_ptr->source)) != 0);
    return CODE_OK;
}

/** @brief module_index 已由入口完成模块和等级过滤；模块表保持有效直至 writer 退出。 */
inline static DOMINO_CODE dominoLogInitQueueItem(DominoLogger* logger_ptr, DominoLogQueueItem* item_ptr, uint8_t module_index, DOMINO_LOG_LEVEL level,
                                                 DominoLogSourceLocation loc) {
    item_ptr->module_index = module_index;
    item_ptr->seq = atomic_fetch_add(&logger_ptr->seq, 1U) + 1U;
    item_ptr->tid = dominoGetNumericThreadId();
    item_ptr->level = level;
    return dominoLogSetSourceLocation(item_ptr, loc);
}

/** @brief 参数均由 writer 提供且非空；line_out_size 为 DOMINO_LOG_MESSAGE_MAX，足够容纳截断尾、换行和 NUL。 */
static DOMINO_CODE dominoLogBuildOutputLine(const DominoLogQueueItem* item_ptr, const char* ts_ptr, const char* module_name, char* line_out_ptr,
                                            size_t line_out_size, size_t* line_len_out_ptr) {
    const size_t content_capacity = line_out_size - 2U;  // 为换行和结尾 NUL 预留空间。
    const char* level_name = dominoLogLevelName(item_ptr->level);
    int prefix_result = snprintf(line_out_ptr, line_out_size, "[%s][tid=%" PRIu32 "][seq=%" PRIu32 "][%s][%s][%s] ", ts_ptr, item_ptr->tid,
                                 item_ptr->seq, level_name, module_name, item_ptr->source);

    bool truncated = item_ptr->source_truncated;
    size_t used_len = 0U;
    if (prefix_result < 0) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_STRING_FORMAT, "log output prefix formatting failed");
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
    *line_len_out_ptr = used_len;
    return CODE_OK;
}

__attribute__((hot)) static DOMINO_CODE dominoLogWriteOneWithTimestamp(const DominoLogger* logger_ptr, const DominoLogQueueItem* item_ptr,
                                                                       const char* ts_ptr) {
    const char* module_name = logger_ptr->modules[item_ptr->module_index].name;
    char line_buf[DOMINO_LOG_MESSAGE_MAX] = {0};
    size_t out_len;
    DOMINO_CODE code = dominoLogBuildOutputLine(item_ptr, ts_ptr, module_name, line_buf, sizeof(line_buf), &out_len);
    if (code != CODE_OK) {
        return code;
    }

    FILE* out_ptr = (item_ptr->level >= DOMINO_LOG_LEVEL_ERROR) ? stderr : stdout;
    if (fwrite(line_buf, 1, out_len, out_ptr) != out_len) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_FILE_WRITE, "log console write failed");
    }

    if (logger_ptr->file_ptr != nullptr) {
        if (fwrite(line_buf, 1, out_len, logger_ptr->file_ptr) != out_len) {
            return DOMINO_LOG_DIRECT_ERROR(ERR_FILE_WRITE, "log file write failed");
        }
    }
    return CODE_OK;
}

static int dominoLogWriterThread(void* arg_ptr) {
    DominoLogger* logger_ptr = arg_ptr;
    DominoLogQueueItem items[DOMINO_LOG_WRITER_BATCH_MAX];
    DOMINO_CODE result_code = CODE_OK;

    while (true) {
        result_code = (DOMINO_CODE)atomic_load(&logger_ptr->lifecycle_result);
        if (result_code != CODE_OK) {
            goto completed;
        }
        uint32_t batch_count;
        result_code = dominoThreadQueueConsumeBatch(logger_ptr->queue_ptr, items, DOMINO_LOG_WRITER_BATCH_MAX, &batch_count, true);
        if (result_code == ERR_QUEUE_PRODUCER_STOPPED) {
            result_code = CODE_OK;
            break;
        }
        if (result_code != CODE_OK) {
            break;
        }

        char ts_buf[32] = {0};
        result_code = dominoFormatUtcTimestampNow(ts_buf, sizeof(ts_buf));
        if (result_code != CODE_OK) {
            (void)DOMINO_LOG_DIRECT_ERROR(result_code, "log timestamp generation failed");
            goto completed;
        }
        for (size_t i = 0U; i < batch_count; i++) {
            result_code = (DOMINO_CODE)atomic_load(&logger_ptr->lifecycle_result);
            if (result_code != CODE_OK) {
                goto completed;
            }
            result_code = dominoLogWriteOneWithTimestamp(logger_ptr, &items[i], ts_buf);
            if (result_code != CODE_OK) {
                goto completed;
            }
        }
    }

completed:
    if (result_code != CODE_OK) {
        result_code = dominoLogFailRuntime(logger_ptr, result_code);
    } else {
        result_code = (DOMINO_CODE)atomic_load(&logger_ptr->lifecycle_result);
    }
    atomic_store(&logger_ptr->writer_completed_flag, true);
    return result_code;
}

static int dominoLogValidateModules(const DominoLogModule* modules_ptr, size_t module_count) {
    if (module_count > (size_t)DOMINO_LOG_MAX_MODULES) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_CAPACITY_EXCEEDED, "log module count exceeds fixed module capacity");
    }
    if (module_count > 0U && modules_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "logger or module list pointer is null");
    }
    for (size_t module_index = 0U; module_index < module_count; module_index++) {
        const DominoLogModule* module_ptr = &modules_ptr[module_index];
        if (module_ptr->name[0] == '\0' || memchr(module_ptr->name, '\0', sizeof(module_ptr->name)) == nullptr) {
            return DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "log module name or minimum level is invalid");
        }
        if (module_ptr->min_level != 0 && !dominoLogLevelIsValid(module_ptr->min_level)) {
            return DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "log module name or minimum level is invalid");
        }
        for (size_t previous_index = 0U; previous_index < module_index; previous_index++) {
            if (modules_ptr[previous_index].module_id == module_ptr->module_id || strcmp(modules_ptr[previous_index].name, module_ptr->name) == 0) {
                return DOMINO_LOG_DIRECT_ERROR(ERR_ALREADY_EXISTS, "log module ID or name is duplicated");
            }
        }
    }
    return CODE_OK;
}

int dominoLogInit(DominoLogConfig config, const DominoLogModule* modules_ptr, size_t module_count, DominoLogger* logger_ptr) {
    if (logger_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "logger or module list pointer is null");
    }
    memset(logger_ptr, 0, sizeof(*logger_ptr));
    atomic_init(&logger_ptr->seq, 0U);
    atomic_init(&logger_ptr->lifecycle_result, CODE_OK);
    atomic_init(&logger_ptr->writer_completed_flag, false);
    if (config.min_level != 0 && !dominoLogLevelIsValid(config.min_level)) {
        return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "log default level is invalid"));
    }
    int result = dominoLogValidateModules(modules_ptr, module_count);
    if (result != CODE_OK) {
        return dominoLogFailLifecycle(logger_ptr, result);
    }
    size_t capacity = config.buffer_size == 0U ? (size_t)DOMINO_LOG_DEFAULT_QUEUE_CAPACITY : config.buffer_size;
    if (capacity > SIZE_MAX / sizeof(DominoLogQueueItem)) {
        return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_MEMORY_OVERFLOW, "log queue allocation size overflows size_t"));
    }
    if (capacity > UINT32_MAX) {
        return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_CAPACITY_EXCEEDED, "log queue capacity exceeds uint32_t capacity"));
    }

    logger_ptr->default_min_level = config.min_level == 0 ? DOMINO_LOG_DEFAULT_MIN_LEVEL : config.min_level;
    for (size_t module_index = 0U; module_index < module_count; module_index++) {
        DominoLogModule module = modules_ptr[module_index];
        module.min_level = module.min_level == 0 ? logger_ptr->default_min_level : module.min_level;
        logger_ptr->modules[module_index] = module;
    }
    logger_ptr->module_count = module_count;

    logger_ptr->queue_ptr = calloc(1U, sizeof(*logger_ptr->queue_ptr));
    if (logger_ptr->queue_ptr == nullptr) {
        return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_MEMORY_ALLOC, "log queue allocation failed"));
    }
    result = dominoThreadQueueInit(logger_ptr->queue_ptr, sizeof(DominoLogQueueItem), (uint32_t)capacity, (uint32_t)capacity);
    if (result != CODE_OK) {
        return dominoLogFailLifecycle(logger_ptr, result);
    }

    if (config.file_path != nullptr && config.file_path[0] != '\0') {
        logger_ptr->file_ptr = fopen(config.file_path, "ab");
        if (logger_ptr->file_ptr == nullptr) {
            return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_FILE_OPEN, "log file open failed"));
        }
    }
    if (thrd_create(&logger_ptr->writer_thread, dominoLogWriterThread, logger_ptr) != thrd_success) {
        return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_THREAD_CREATE, "log writer thread creation failed"));
    }

    return CODE_OK;
}

int dominoLogDestroy(DominoLogger* logger_ptr) {
    if (logger_ptr == nullptr) {
        return CODE_OK;
    }
    int lifecycle_result = atomic_load(&logger_ptr->lifecycle_result);
    if (lifecycle_result != CODE_OK) {
        return lifecycle_result;
    }
    if (logger_ptr->queue_ptr == nullptr) {
        return CODE_OK;
    }

    DOMINO_CODE result = dominoThreadQueueStopAndWake(logger_ptr->queue_ptr, DOMINO_THREAD_QUEUE_PRODUCER);
    if (result != CODE_OK) {
        return dominoLogFailLifecycle(logger_ptr, result);
    }
    const struct timespec poll_interval = {.tv_sec = 0, .tv_nsec = 1000000L};
    while (!atomic_load(&logger_ptr->writer_completed_flag)) {
        lifecycle_result = atomic_load(&logger_ptr->lifecycle_result);
        if (lifecycle_result != CODE_OK) {
            return lifecycle_result;
        }
        if (thrd_sleep(&poll_interval, nullptr) == -2) {
            return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_THREAD_SLEEP, "log writer completion polling sleep failed"));
        }
    }
    lifecycle_result = atomic_load(&logger_ptr->lifecycle_result);
    if (lifecycle_result != CODE_OK) {
        return lifecycle_result;
    }
    int writer_result = CODE_OK;
    if (thrd_join(logger_ptr->writer_thread, &writer_result) != thrd_success) {
        return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_THREAD_JOIN, "log writer thread join failed"));
    }
    if (writer_result != CODE_OK) {
        return dominoLogFailLifecycle(logger_ptr, writer_result);
    }

    if (fflush(stdout) != 0) {
        return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_FILE_FLUSH, "log stdout flush failed"));
    }
    if (fflush(stderr) != 0) {
        return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_FILE_FLUSH, "log stderr flush failed"));
    }
    if (logger_ptr->file_ptr != nullptr) {
        if (fflush(logger_ptr->file_ptr) != 0) {
            return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_FILE_FLUSH, "log file flush failed"));
        }
        if (fclose(logger_ptr->file_ptr) != 0) {
            return dominoLogFailLifecycle(logger_ptr, DOMINO_LOG_DIRECT_ERROR(ERR_FILE_CLOSE, "log file close failed"));
        }
        logger_ptr->file_ptr = nullptr;
    }

    result = dominoThreadQueueDestroy(logger_ptr->queue_ptr);
    if (result != CODE_OK) {
        return dominoLogFailLifecycle(logger_ptr, result);
    }
    free(logger_ptr->queue_ptr);

    memset(logger_ptr, 0, sizeof(*logger_ptr));
    atomic_init(&logger_ptr->seq, 0U);
    atomic_init(&logger_ptr->lifecycle_result, CODE_OK);
    atomic_init(&logger_ptr->writer_completed_flag, false);
    return CODE_OK;
}

/** @brief 返回通过等级过滤的模块下标；空实例、失败实例、非法等级或未匹配模块返回 -1。 */
static int dominoLogFindEnabledModule(const DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level) {
    if (logger_ptr == nullptr || atomic_load(&logger_ptr->lifecycle_result) != CODE_OK || !dominoLogLevelIsValid(level)) {
        return -1;
    }
    for (size_t i = 0U; i < logger_ptr->module_count; i++) {
        if (logger_ptr->modules[i].module_id == module_id) {
            return level >= logger_ptr->modules[i].min_level ? (int)i : -1;
        }
    }
    return -1;
}

void dominoLogVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt,
                 va_list arg_list) {
    if (fmt == nullptr) {
        return;
    }
    const int module_index = dominoLogFindEnabledModule(logger_ptr, module_id, level);
    if (module_index < 0) {
        return;
    }

    DominoLogQueueItem item = {0};
    DOMINO_CODE code = dominoLogInitQueueItem(logger_ptr, &item, (uint8_t)module_index, level, loc);
    if (code != CODE_OK) {
        (void)dominoLogFailRuntime(logger_ptr, code);
        return;
    }

    va_list arg_copy;
    va_copy(arg_copy, arg_list);
    int formatted_len = vsnprintf(item.body, (size_t)DOMINO_LOG_MESSAGE_MAX, fmt, arg_copy);
    va_end(arg_copy);
    if (formatted_len < 0) {
        code = DOMINO_LOG_DIRECT_ERROR(ERR_STRING_FORMAT, "log message formatting failed");
        (void)dominoLogFailRuntime(logger_ptr, code);
        return;
    }
    if (formatted_len == 0) {
        return;
    }

    item.body_len = (size_t)formatted_len < DOMINO_LOG_MESSAGE_MAX ? (size_t)formatted_len : DOMINO_LOG_MESSAGE_MAX - 1U;
    dominoLogProduce(logger_ptr, &item);
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
    if (msg_ptr == nullptr || msg_len == 0U) {
        return;
    }
    const int module_index = dominoLogFindEnabledModule(logger_ptr, module_id, level);
    if (module_index < 0) {
        return;
    }

    DominoLogQueueItem item = {0};
    DOMINO_CODE code = dominoLogInitQueueItem(logger_ptr, &item, (uint8_t)module_index, level, loc);
    if (code != CODE_OK) {
        (void)dominoLogFailRuntime(logger_ptr, code);
        return;
    }

    size_t copy_len = msg_len;
    if (copy_len >= (size_t)DOMINO_LOG_MESSAGE_MAX) {
        copy_len = (size_t)DOMINO_LOG_MESSAGE_MAX - 1U;
    }
    memcpy(item.body, msg_ptr, copy_len);
    item.body[copy_len] = '\0';
    item.body_len = copy_len;

    dominoLogProduce(logger_ptr, &item);
}
