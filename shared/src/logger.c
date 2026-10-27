#include <inttypes.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "domino_shared_common.h"
#include "domino_shared_error_codes.h"
#include "domino_shared_logger.h"
#include "domino_shared_queue.h"

#define DOMINO_LOG_DEFAULT_MIN_LEVEL DOMINO_LOG_LEVEL_INFO
#define DOMINO_LOG_DEFAULT_QUEUE_CAPACITY 10240U

#define DOMINO_LOG_TRUNC_SUFFIX "...(TRUNCATED)"
#define DOMINO_LOG_TRUNC_SUFFIX_LEN (sizeof(DOMINO_LOG_TRUNC_SUFFIX) - 1U)

#define DOMINO_LOG_WRITER_BATCH_MAX 128U

/*包含结尾'\0'*/
#define DOMINO_LOG_MESSAGE_MAX 256U
#define DOMINO_LOG_TIMESTAMP_MAX 32U
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
    return (bool)(level >= DOMINO_LOG_LEVEL_TRACE && level <= DOMINO_LOG_LEVEL_ERROR);
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
    char timestamp_str[DOMINO_LOG_TIMESTAMP_MAX];  // 生产者在整理来源、正文和等待入队前采样的 UTC 时间。
    char source[DOMINO_LOG_SOURCE_MAX];
    char body[DOMINO_LOG_MESSAGE_MAX];  // 包含结尾'\0'
} DominoLogQueueItem;

static_assert(offsetof(DominoLogQueueItem, id) == 0U, "queue ID must be the first log item field");

/* 线程编号在进程内共享，同一生产线程写不同实例时保持相同 tid。 */
static _Atomic uint32_t g_tid_next = 0U;
static thread_local uint32_t g_tid_cached = 0U;

/** @brief 满队列直接提示一次并阻塞入队；其他错误直接终止进程。 */
static void dominoLogProduce(DominoLogger* logger_ptr, DominoLogQueueItem* item_ptr) {
    DOMINO_CODE code = dominoThreadQueueProduce(logger_ptr->queue_ptr, item_ptr, false);
    if (code == ERR_QUEUE_FULL) {
        FILE* output_ptr = logger_ptr->modules[item_ptr->module_index].output_ptr;
        if (fputs("[WARN] log queue is full; waiting for space\n", output_ptr) == EOF) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_WRITE, "log queue warning write failed");
        }
        if (fflush(output_ptr) != 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_FLUSH, "log queue warning flush failed");
        }
        code = dominoThreadQueueProduce(logger_ptr->queue_ptr, item_ptr, true);
    }
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "log message could not enter the writer queue");
    }
}

static uint32_t dominoGetNumericThreadId(void) {
    // 后续改造为跨平台，系统线程ID
    if (g_tid_cached != 0U) {
        return g_tid_cached;
    }
    uint32_t thread_id = atomic_fetch_add(&g_tid_next, 1U) + 1U;
    if (thread_id == 0U) {
        DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "log thread IDs are exhausted");
    }
    g_tid_cached = thread_id;
    return thread_id;
}

inline static size_t dominoLogBoundedStringLength(const char* value_ptr, size_t max_len, bool* truncated_out_ptr) {
    size_t value_len = 0U;
    while (value_len < max_len && value_ptr[value_len] != '\0') {
        value_len++;
    }
    *truncated_out_ptr = (bool)(value_ptr[value_len] != '\0');
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
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "log source location formatting failed");
    }
    item_ptr->source_truncated = file_truncated || func_truncated || (size_t)source_result >= sizeof(item_ptr->source);
}

/** @brief module_index 已由入口完成模块和等级过滤；模块表保持有效直至 writer 退出。 */
inline static void dominoLogInitQueueItem(DominoLogger* logger_ptr, DominoLogQueueItem* item_ptr, uint8_t module_index, DOMINO_LOG_LEVEL level,
                                          DominoLogSourceLocation loc) {
    DOMINO_CODE code = dominoFormatUtcTimestampNow(item_ptr->timestamp_str, sizeof(item_ptr->timestamp_str));
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "log timestamp generation failed");
    }
    item_ptr->module_index = module_index;
    item_ptr->seq = atomic_fetch_add(&logger_ptr->seq, 1U) + 1U;
    item_ptr->tid = dominoGetNumericThreadId();
    item_ptr->level = level;
    dominoLogSetSourceLocation(item_ptr, loc);
}

/** @brief 参数均由 writer 提供且非空；line_out_size 为 DOMINO_LOG_MESSAGE_MAX，足够容纳截断尾、换行和 NUL。 */
static void dominoLogBuildOutputLine(const DominoLogQueueItem* item_ptr, const char* module_name, char* line_out_ptr, size_t line_out_size,
                                     size_t* line_len_out_ptr) {
    const size_t content_capacity = line_out_size - 2U;  // 为换行和结尾 NUL 预留空间。
    const char* level_name = dominoLogLevelName(item_ptr->level);
    int prefix_result = snprintf(line_out_ptr, line_out_size, "[%s][tid=%" PRIu32 "][seq=%" PRIu32 "][%s][%s][%s] ", item_ptr->timestamp_str,
                                 item_ptr->tid, item_ptr->seq, level_name, module_name, item_ptr->source);

    bool truncated = item_ptr->source_truncated;
    size_t used_len = 0U;
    if (prefix_result < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "log output prefix formatting failed");
    } else if ((size_t)prefix_result > content_capacity) {
        used_len = content_capacity;
        truncated = true;
    } else {
        used_len = (size_t)prefix_result;

        const size_t remaining_len = content_capacity - used_len;
        size_t body_copy_len = item_ptr->body_len;
        if (body_copy_len > remaining_len) {
            body_copy_len = remaining_len;
            truncated = true;
        }

        memcpy(line_out_ptr + used_len, item_ptr->body, body_copy_len);
        used_len += body_copy_len;
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
}

__attribute__((hot)) static int dominoLogWriterThread(void* arg_ptr) {
    DominoLogger* logger_ptr = arg_ptr;
    DominoLogQueueItem items[DOMINO_LOG_WRITER_BATCH_MAX];
    while (true) {
        uint32_t batch_count;
        DOMINO_CODE result_code = dominoThreadQueueConsumeBatch(logger_ptr->queue_ptr, items, DOMINO_LOG_WRITER_BATCH_MAX, &batch_count, true);
        if (result_code == ERR_QUEUE_FLUSHING) {
            return CODE_OK;
        }
        if (result_code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(result_code, "log writer queue consumption failed");
        }

        for (size_t i = 0U; i < batch_count; i++) {
            const DominoLogQueueItem* item_ptr = &items[i];
            const DominoLogModule* module_ptr = &logger_ptr->modules[item_ptr->module_index];
            char line_buf[DOMINO_LOG_MESSAGE_MAX];
            size_t out_len;
            dominoLogBuildOutputLine(item_ptr, module_ptr->name, line_buf, sizeof(line_buf), &out_len);

            if (fwrite(line_buf, 1, out_len, module_ptr->output_ptr) != out_len) {
                DOMINO_EXIT_WITH_ERROR(ERR_FILE_WRITE, "log output write failed");
            }
        }
    }
}

static void dominoLogValidateModules(const DominoLogModule* modules_ptr, size_t module_count) {
    if (module_count > (size_t)DOMINO_LOG_MAX_MODULES) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "log module count exceeds fixed module capacity");
    }
    if (module_count > 0U && modules_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "logger or module list pointer is null");
    }
    for (size_t module_index = 0U; module_index < module_count; module_index++) {
        const DominoLogModule* module_ptr = &modules_ptr[module_index];
        if (module_ptr->name[0] == '\0' || memchr(module_ptr->name, '\0', sizeof(module_ptr->name)) == nullptr) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "log module name or minimum level is invalid");
        }
        if (module_ptr->min_level != 0 && !dominoLogLevelIsValid(module_ptr->min_level)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "log module name or minimum level is invalid");
        }
        for (size_t previous_index = 0U; previous_index < module_index; previous_index++) {
            if (modules_ptr[previous_index].module_id == module_ptr->module_id || strcmp(modules_ptr[previous_index].name, module_ptr->name) == 0) {
                DOMINO_EXIT_WITH_ERROR(ERR_ALREADY_EXISTS, "log module ID or name is duplicated");
            }
        }
    }
}

void dominoLogInit(DominoLogConfig config, const DominoLogModule* modules_ptr, size_t module_count, DominoLogger* logger_ptr) {
    if (logger_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "logger or module list pointer is null");
    }
    if (logger_ptr->lifecycle_state != DOMINO_MODULE_UNINITIALIZED) {
        DOMINO_EXIT_WITH_ERROR(ERR_ALREADY_INITIALIZED, "log initialization requires an uninitialized instance");
    }
    atomic_init(&logger_ptr->seq, 0U);
    if (config.min_level != 0 && !dominoLogLevelIsValid(config.min_level)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "log default level is invalid");
    }
    dominoLogValidateModules(modules_ptr, module_count);
    size_t capacity = config.buffer_size == 0U ? (size_t)DOMINO_LOG_DEFAULT_QUEUE_CAPACITY : config.buffer_size;
    if (capacity > SIZE_MAX / sizeof(DominoLogQueueItem)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "log queue allocation size overflows size_t");
    }
    if (capacity > UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "log queue capacity exceeds uint32_t capacity");
    }

    logger_ptr->queue_ptr = calloc(1U, sizeof(*logger_ptr->queue_ptr));
    if (logger_ptr->queue_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "log queue allocation failed");
    }
    dominoThreadQueueInit(logger_ptr->queue_ptr, sizeof(DominoLogQueueItem), (uint32_t)capacity, (uint32_t)capacity);

    logger_ptr->file_ptr = dominoOpenAppendFile(config.file_path);
    FILE* default_output_ptr = logger_ptr->file_ptr != nullptr ? logger_ptr->file_ptr : stdout;
    const DOMINO_LOG_LEVEL default_min_level = config.min_level == 0 ? DOMINO_LOG_DEFAULT_MIN_LEVEL : config.min_level;
    for (size_t module_index = 0U; module_index < module_count; module_index++) {
        const DominoLogModule* config_ptr = &modules_ptr[module_index];
        DominoLogModule* module_ptr = &logger_ptr->modules[module_index];
        // 首次零初始化或 Destroy 已建立空状态；只复制配置，不保留 file_path 或采用传入的 output_ptr。
        memcpy(module_ptr->name, config_ptr->name, sizeof(module_ptr->name));
        module_ptr->module_id = config_ptr->module_id;
        module_ptr->min_level = config_ptr->min_level == 0 ? default_min_level : config_ptr->min_level;
        module_ptr->output_ptr = dominoOpenAppendFile(config_ptr->file_path);
        if (module_ptr->output_ptr == nullptr) {
            module_ptr->output_ptr = default_output_ptr;
        }
    }
    logger_ptr->module_count = module_count;
    logger_ptr->lifecycle_state = DOMINO_MODULE_INITIALIZED;
}

void dominoLogStart(DominoLogger* logger_ptr) {
    if (logger_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "log start requires an instance");
    }
    if (logger_ptr->lifecycle_state != DOMINO_MODULE_INITIALIZED) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "log writer start requires an initialized instance that has not stopped");
    }
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&logger_ptr->writer_thread, dominoLogWriterThread, logger_ptr), ERR_THREAD_CREATE,
                                  "log writer thread creation failed");
    logger_ptr->lifecycle_state = DOMINO_MODULE_STARTED;
}

void dominoLogStop(DominoLogger* logger_ptr) {
    if (logger_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "log stop requires an instance");
    }
    if (logger_ptr->lifecycle_state != DOMINO_MODULE_INITIALIZED && logger_ptr->lifecycle_state != DOMINO_MODULE_STARTED) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "log stop requires an initialized or started instance that has not stopped");
    }

    dominoThreadQueueBeginFlush(logger_ptr->queue_ptr);
    if (logger_ptr->lifecycle_state == DOMINO_MODULE_STARTED) {
        DOMINO_REQUIRE_THREAD_SUCCESS(thrd_join(logger_ptr->writer_thread, nullptr), ERR_THREAD_JOIN, "log writer thread join failed");
    }

    bool stdout_used = false;
    for (size_t module_index = 0U; module_index < logger_ptr->module_count; module_index++) {
        FILE* output_ptr = logger_ptr->modules[module_index].output_ptr;
        if (output_ptr == stdout) {
            stdout_used = true;
        } else if (output_ptr != logger_ptr->file_ptr && dominoFlushFile(output_ptr) != 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_FLUSH, "log module file flush or fsync failed");
        }
    }
    if (logger_ptr->file_ptr != nullptr && dominoFlushFile(logger_ptr->file_ptr) != 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_FLUSH, "log default file flush or fsync failed");
    }
    if (stdout_used && fflush(stdout) != 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_FLUSH, "log stdout flush failed");
    }
    logger_ptr->lifecycle_state = DOMINO_MODULE_STOPPED;
}

void dominoLogDestroy(DominoLogger* logger_ptr) {
    if (logger_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_NULL_POINTER, "log destroy requires an instance");
    }
    if (logger_ptr->lifecycle_state != DOMINO_MODULE_STOPPED) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "log destroy requires a stopped instance");
    }

    for (size_t module_index = 0U; module_index < logger_ptr->module_count; module_index++) {
        FILE* output_ptr = logger_ptr->modules[module_index].output_ptr;
        if (output_ptr != stdout && output_ptr != logger_ptr->file_ptr) {
            dominoFlushAndCloseFile(output_ptr);
        }
    }
    if (logger_ptr->file_ptr != nullptr) {
        dominoFlushAndCloseFile(logger_ptr->file_ptr);
    }

    dominoThreadQueueDestroy(logger_ptr->queue_ptr);
    free(logger_ptr->queue_ptr);

    memset(logger_ptr, 0, sizeof(*logger_ptr));
    atomic_init(&logger_ptr->seq, 0U);
}

/** @brief 返回通过等级过滤的模块下标；未匹配或被过滤时返回 -1。
 * @pre LogVa 已确认 logger_ptr 非空且 level 有效；实例存储为首次零初始化或尚未释放的有效对象。
 */
static int dominoLogFindEnabledModule(const DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level) {
    for (size_t i = 0U; i < logger_ptr->module_count; i++) {
        if (logger_ptr->modules[i].module_id == module_id) {
            return level >= logger_ptr->modules[i].min_level ? (int)i : -1;
        }
    }
    return -1;
}

void dominoLogVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt,
                 va_list arg_list) {
    if (logger_ptr == nullptr || fmt == nullptr || !dominoLogLevelIsValid(level)) {
        return;
    }
    const int module_index = dominoLogFindEnabledModule(logger_ptr, module_id, level);
    if (module_index < 0) {
        return;
    }
    if (logger_ptr->lifecycle_state != DOMINO_MODULE_STARTED) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "log message requires a started writer before stop");
    }

    DominoLogQueueItem item = {0};
    dominoLogInitQueueItem(logger_ptr, &item, (uint8_t)module_index, level, loc);

    va_list arg_copy;
    va_copy(arg_copy, arg_list);
    int formatted_len = vsnprintf(item.body, (size_t)DOMINO_LOG_MESSAGE_MAX, fmt, arg_copy);
    va_end(arg_copy);
    if (formatted_len < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "log message formatting failed");
    }
    if (formatted_len == 0) {
        return;
    }

    item.body_len = (size_t)formatted_len < DOMINO_LOG_MESSAGE_MAX ? (size_t)formatted_len : DOMINO_LOG_MESSAGE_MAX - 1U;
    dominoLogProduce(logger_ptr, &item);
}
