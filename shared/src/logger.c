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
#define DOMINO_LOG_DEFAULT_QUEUE_CAPACITY 20000U

#define DOMINO_LOG_TRUNCATION_MARK "...(TRUNCATED)"
#define DOMINO_LOG_TRUNCATION_MARK_LEN (sizeof(DOMINO_LOG_TRUNCATION_MARK) - 1U)

#define DOMINO_LOG_WRITER_BATCH_MAX 128U

/* 以下缓冲大小均含结尾 NUL；正文缓冲与最终输出行同为 DOMINO_LOG_LINE_SIZE。 */
#define DOMINO_LOG_LINE_SIZE 256U
#define DOMINO_LOG_TIMESTAMP_SIZE 32U
#define DOMINO_LOG_SOURCE_SIZE 128U
#define DOMINO_LOG_SOURCE_FILE_CAPTURE_MAX 80U
#define DOMINO_LOG_SOURCE_FUNC_CAPTURE_MAX 32U

static_assert(DOMINO_LOG_LINE_SIZE > (DOMINO_LOG_TRUNCATION_MARK_LEN + 2U), "DOMINO_LOG_LINE_SIZE too small for truncation mark");
static_assert(DOMINO_LOG_SOURCE_FILE_CAPTURE_MAX + DOMINO_LOG_SOURCE_FUNC_CAPTURE_MAX + 16U <= DOMINO_LOG_SOURCE_SIZE,
              "DOMINO_LOG_SOURCE_SIZE is too small for bounded source components");
static_assert(DOMINO_LOG_DEFAULT_QUEUE_CAPACITY > 1U, "default queue capacity must be positive");
static_assert(DOMINO_LOG_WRITER_BATCH_MAX > 1U, "writer batch max must be positive");
static_assert(DOMINO_LOG_MODULE_COUNT_MAX > 1U, "max modules must be positive");
static_assert(DOMINO_LOG_MODULE_COUNT_MAX <= UINT8_MAX + 1U, "log module index must fit in uint8_t");

inline static bool isLogLevelValid(DOMINO_LOG_LEVEL level) {
    return (bool)(level >= DOMINO_LOG_LEVEL_TRACE && level <= DOMINO_LOG_LEVEL_ERROR);
}

inline static const char* getLogLevelName(DOMINO_LOG_LEVEL level) {
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

/** @brief 队列中的一条日志；全部内容按值保存，writer 不借用调用方内存。 */
typedef struct DominoLogRecord {
    uint64_t id;      ///< 队列消息 ID，与日志序号 seq 无关。
    size_t body_len;  ///< 不含结尾 NUL。
    uint32_t seq;
    uint32_t tid;
    DOMINO_LOG_LEVEL level;
    uint8_t module_index;  ///< 生产者过滤后得到的模块表下标。
    bool source_truncated_flag;
    char timestamp_str[DOMINO_LOG_TIMESTAMP_SIZE];  ///< 调用线程在入队前采样的 UTC 时间。
    char source_str[DOMINO_LOG_SOURCE_SIZE];
    char body_str[DOMINO_LOG_LINE_SIZE];
} DominoLogRecord;

static_assert(offsetof(DominoLogRecord, id) == 0U, "queue ID must be the first log record field");

/* 日志线程编号在进程内分配并按线程缓存，同一线程写不同实例时编号相同；它不是操作系统线程 ID。 */
static _Atomic uint32_t g_last_log_thread_id = 0U;
static thread_local uint32_t g_cached_log_thread_id = 0U;

/** @brief 先非阻塞入队；队列满时向本模块输出流提示一次，再阻塞等待空位。 */
static void enqueueLogRecord(DominoLogger* logger_ptr, DominoLogRecord* record_ptr) {
    DOMINO_CODE code = dominoThreadQueueProduce(logger_ptr->queue_ptr, record_ptr, false);
    if (code == ERR_QUEUE_FULL) {
        FILE* output_ptr = logger_ptr->module_list[record_ptr->module_index].output_ptr;
        if (fputs("[WARN] log queue is full; waiting for space\n", output_ptr) == EOF) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_WRITE, "log queue warning write failed");
        }
        if (fflush(output_ptr) != 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_FLUSH, "log queue warning flush failed");
        }
        code = dominoThreadQueueProduce(logger_ptr->queue_ptr, record_ptr, true);
    }
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "log message could not enter the writer queue");
    }
}

static uint32_t getLogThreadId(void) {
    if (g_cached_log_thread_id != 0U) {
        return g_cached_log_thread_id;
    }
    uint32_t thread_id = atomic_fetch_add(&g_last_log_thread_id, 1U) + 1U;
    if (thread_id == 0U) {
        DOMINO_EXIT_WITH_ERROR(ERR_ID_EXHAUSTED, "log thread IDs are exhausted");
    }
    g_cached_log_thread_id = thread_id;
    return thread_id;
}

/** @brief 至多数到 max_len 个字符；还有剩余字符时置截断标志。 */
inline static size_t measureBoundedString(const char* value_ptr, size_t max_len, bool* truncated_out_ptr) {
    size_t value_len = 0U;
    while (value_len < max_len && value_ptr[value_len] != '\0') {
        value_len++;
    }
    *truncated_out_ptr = (bool)(value_ptr[value_len] != '\0');
    return value_len;
}

static void formatLogSource(DominoLogRecord* record_ptr, DominoLogSourceLocation loc) {
    const char* file_ptr = loc.file != nullptr ? loc.file : "?";
    const char* func_ptr = loc.func != nullptr ? loc.func : "?";
    bool file_truncated_flag = false;
    bool func_truncated_flag = false;
    size_t file_len = measureBoundedString(file_ptr, DOMINO_LOG_SOURCE_FILE_CAPTURE_MAX, &file_truncated_flag);
    size_t func_len = measureBoundedString(func_ptr, DOMINO_LOG_SOURCE_FUNC_CAPTURE_MAX, &func_truncated_flag);
    int line_num = loc.line > 0 ? loc.line : 0;

    int written =
        snprintf(record_ptr->source_str, sizeof(record_ptr->source_str), "%.*s:%d:%.*s", (int)file_len, file_ptr, line_num, (int)func_len, func_ptr);
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "log source location formatting failed");
    }
    record_ptr->source_truncated_flag = file_truncated_flag || func_truncated_flag || (size_t)written >= sizeof(record_ptr->source_str);
}

/** @brief 采样调用时刻并填写序号、线程编号、等级和来源；module_index 已通过过滤。 */
inline static void fillLogRecordHeader(DominoLogger* logger_ptr, DominoLogRecord* record_ptr, uint8_t module_index, DOMINO_LOG_LEVEL level,
                                       DominoLogSourceLocation loc) {
    DOMINO_CODE code = dominoFormatUtcTimestampNow(record_ptr->timestamp_str, sizeof(record_ptr->timestamp_str));
    if (code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(code, "log timestamp generation failed");
    }
    record_ptr->module_index = module_index;
    record_ptr->seq = atomic_fetch_add(&logger_ptr->last_seq, 1U) + 1U;
    record_ptr->tid = getLogThreadId();
    record_ptr->level = level;
    formatLogSource(record_ptr, loc);
}

/** @brief 拼出“前缀 + 正文 + 换行”；超长时在行尾保留截断标记。
 * @param line_size 缓冲大小，等于 DOMINO_LOG_LINE_SIZE，足够容纳截断标记、换行和 NUL。
 */
static void buildLogOutputLine(const DominoLogRecord* record_ptr, const char* module_name, char* line_ptr, size_t line_size, size_t* line_len_ptr) {
    const size_t content_capacity = line_size - 2U;  // 预留换行和 NUL。
    int prefix_len = snprintf(line_ptr, line_size, "[%s][tid=%" PRIu32 "][seq=%" PRIu32 "][%s][%s][%s] ", record_ptr->timestamp_str, record_ptr->tid,
                              record_ptr->seq, getLogLevelName(record_ptr->level), module_name, record_ptr->source_str);

    bool truncated_flag = record_ptr->source_truncated_flag;
    size_t used_len = 0U;
    if (prefix_len < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "log output prefix formatting failed");
    } else if ((size_t)prefix_len > content_capacity) {
        used_len = content_capacity;
        truncated_flag = true;
    } else {
        used_len = (size_t)prefix_len;
        size_t body_copy_len = record_ptr->body_len;
        if (body_copy_len > content_capacity - used_len) {
            body_copy_len = content_capacity - used_len;
            truncated_flag = true;
        }
        memcpy(line_ptr + used_len, record_ptr->body_str, body_copy_len);
        used_len += body_copy_len;
    }

    if (truncated_flag) {
        const size_t max_content_len = content_capacity - DOMINO_LOG_TRUNCATION_MARK_LEN;
        used_len = used_len < max_content_len ? used_len : max_content_len;
        memcpy(line_ptr + used_len, DOMINO_LOG_TRUNCATION_MARK, DOMINO_LOG_TRUNCATION_MARK_LEN);
        used_len += DOMINO_LOG_TRUNCATION_MARK_LEN;
    }

    line_ptr[used_len++] = '\n';
    line_ptr[used_len] = '\0';
    *line_len_ptr = used_len;
}

/** @brief 阻塞批量消费并写出，消费到排空结果后退出。 */
__attribute__((hot)) static int logWriterThreadMain(void* arg_ptr) {
    DominoLogger* logger_ptr = arg_ptr;
    DominoLogRecord record_batch[DOMINO_LOG_WRITER_BATCH_MAX];
    while (true) {
        uint32_t batch_count;
        DOMINO_CODE code = dominoThreadQueueConsumeBatch(logger_ptr->queue_ptr, record_batch, DOMINO_LOG_WRITER_BATCH_MAX, &batch_count, true);
        if (code == ERR_QUEUE_FLUSHING) {
            return CODE_OK;
        }
        if (code != CODE_OK) {
            DOMINO_EXIT_WITH_ERROR(code, "log writer queue consumption failed");
        }

        for (size_t batch_index = 0U; batch_index < batch_count; batch_index++) {
            const DominoLogRecord* record_ptr = &record_batch[batch_index];
            const DominoLogModule* module_ptr = &logger_ptr->module_list[record_ptr->module_index];
            char line_buf[DOMINO_LOG_LINE_SIZE];
            size_t line_len;
            buildLogOutputLine(record_ptr, module_ptr->name, line_buf, sizeof(line_buf), &line_len);
            if (fwrite(line_buf, 1, line_len, module_ptr->output_ptr) != line_len) {
                DOMINO_EXIT_WITH_ERROR(ERR_FILE_WRITE, "log output write failed");
            }
        }
    }
}

static void validateLogModules(const DominoLogModule* modules_ptr, size_t module_count) {
    if (module_count > (size_t)DOMINO_LOG_MODULE_COUNT_MAX) {
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
        if (module_ptr->min_level != 0 && !isLogLevelValid(module_ptr->min_level)) {
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
    atomic_init(&logger_ptr->last_seq, 0U);
    if (config.min_level != 0 && !isLogLevelValid(config.min_level)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "log default level is invalid");
    }
    validateLogModules(modules_ptr, module_count);
    size_t capacity = config.queue_capacity == 0U ? (size_t)DOMINO_LOG_DEFAULT_QUEUE_CAPACITY : config.queue_capacity;
    if (capacity > SIZE_MAX / sizeof(DominoLogRecord)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_OVERFLOW, "log queue allocation size overflows size_t");
    }
    if (capacity > UINT32_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "log queue capacity exceeds uint32_t capacity");
    }

    logger_ptr->queue_ptr = calloc(1U, sizeof(*logger_ptr->queue_ptr));
    if (logger_ptr->queue_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "log queue allocation failed");
    }
    // 初始容量等于最大容量：日志积压不会让队列继续扩容（宪法 4.3）。
    dominoThreadQueueInit(logger_ptr->queue_ptr, sizeof(DominoLogRecord), (uint32_t)capacity, (uint32_t)capacity);

    logger_ptr->default_file_ptr = dominoOpenAppendFile(config.file_path);
    FILE* default_output_ptr = logger_ptr->default_file_ptr != nullptr ? logger_ptr->default_file_ptr : stdout;
    const DOMINO_LOG_LEVEL default_min_level = config.min_level == 0 ? DOMINO_LOG_DEFAULT_MIN_LEVEL : config.min_level;
    for (size_t module_index = 0U; module_index < module_count; module_index++) {
        const DominoLogModule* config_ptr = &modules_ptr[module_index];
        DominoLogModule* module_ptr = &logger_ptr->module_list[module_index];
        // 只复制名称、ID 和等级；file_path 不保留，output_ptr 由 logger 解析。
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
    DOMINO_REQUIRE_THREAD_SUCCESS(thrd_create(&logger_ptr->writer_thread, logWriterThreadMain, logger_ptr), ERR_THREAD_CREATE,
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

    // 继承默认文件的模块不单独同步，默认文件最后只同步一次。
    bool stdout_used_flag = false;
    for (size_t module_index = 0U; module_index < logger_ptr->module_count; module_index++) {
        FILE* output_ptr = logger_ptr->module_list[module_index].output_ptr;
        if (output_ptr == stdout) {
            stdout_used_flag = true;
        } else if (output_ptr != logger_ptr->default_file_ptr && dominoSyncFile(output_ptr) != 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_FILE_FLUSH, "log module file flush or fsync failed");
        }
    }
    if (logger_ptr->default_file_ptr != nullptr && dominoSyncFile(logger_ptr->default_file_ptr) != 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_FILE_FLUSH, "log default file flush or fsync failed");
    }
    if (stdout_used_flag && fflush(stdout) != 0) {
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
        FILE* output_ptr = logger_ptr->module_list[module_index].output_ptr;
        if (output_ptr != stdout && output_ptr != logger_ptr->default_file_ptr) {
            dominoFlushAndCloseFile(output_ptr);
        }
    }
    if (logger_ptr->default_file_ptr != nullptr) {
        dominoFlushAndCloseFile(logger_ptr->default_file_ptr);
    }

    dominoThreadQueueDestroy(logger_ptr->queue_ptr);
    free(logger_ptr->queue_ptr);

    memset(logger_ptr, 0, sizeof(*logger_ptr));
    atomic_init(&logger_ptr->last_seq, 0U);
}

/** @brief 返回通过等级过滤的模块下标；未注册或被过滤返回 -1。 */
static int findEnabledLogModule(const DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level) {
    for (size_t module_index = 0U; module_index < logger_ptr->module_count; module_index++) {
        if (logger_ptr->module_list[module_index].module_id == module_id) {
            return level >= logger_ptr->module_list[module_index].min_level ? (int)module_index : -1;
        }
    }
    return -1;
}

void dominoLogVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc, const char* fmt,
                 va_list arg_list) {
    if (logger_ptr == nullptr || fmt == nullptr || !isLogLevelValid(level)) {
        return;
    }
    const int module_index = findEnabledLogModule(logger_ptr, module_id, level);
    if (module_index < 0) {
        return;
    }
    if (logger_ptr->lifecycle_state != DOMINO_MODULE_STARTED) {
        DOMINO_EXIT_WITH_ERROR(ERR_GAME_STATE_INVALID, "log message requires a started writer before stop");
    }

    DominoLogRecord record = {0};
    fillLogRecordHeader(logger_ptr, &record, (uint8_t)module_index, level, loc);

    va_list arg_copy;
    va_copy(arg_copy, arg_list);
    int formatted_len = vsnprintf(record.body_str, sizeof(record.body_str), fmt, arg_copy);
    va_end(arg_copy);
    if (formatted_len < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "log message formatting failed");
    }
    if (formatted_len == 0) {
        return;
    }

    record.body_len = (size_t)formatted_len < sizeof(record.body_str) ? (size_t)formatted_len : sizeof(record.body_str) - 1U;
    enqueueLogRecord(logger_ptr, &record);
}
