#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "domino_engine_log.h"
#include "domino_shared_error_codes.h"

#define TEST_LOG_BUFFER_SIZE (64U * 1024U)
#define TEST_LOG_LINE_MAX 255U
#define TEST_BATCH_RECORD_COUNT 129U

static void logThroughVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc,
                         const char* format_ptr, ...) DOMINO_LOG_PRINTF_ATTR(5, 6);

static void logThroughVa(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc,
                         const char* format_ptr, ...) {
    va_list arg_list;
    va_start(arg_list, format_ptr);
    dominoLogVa(logger_ptr, module_id, level, loc, format_ptr, arg_list);
    va_end(arg_list);
}

static bool readLogFile(const char* path_ptr, char* buffer_ptr, size_t buffer_size, size_t* read_len_out_ptr) {
    FILE* file_ptr = fopen(path_ptr, "rb");
    if (!file_ptr) {
        return false;
    }
    size_t read_len = fread(buffer_ptr, 1U, buffer_size - 1U, file_ptr);
    bool success = !ferror(file_ptr) && feof(file_ptr);
    buffer_ptr[read_len] = '\0';
    *read_len_out_ptr = read_len;
    success = fclose(file_ptr) == 0 && success;
    return success;
}

static bool logLinesAreBounded(const char* buffer_ptr, size_t buffer_len) {
    size_t line_start = 0U;
    for (size_t buffer_index = 0U; buffer_index < buffer_len; buffer_index++) {
        if (buffer_ptr[buffer_index] != '\n') {
            continue;
        }
        size_t line_len = buffer_index - line_start + 1U;
        if (line_len > TEST_LOG_LINE_MAX) {
            return false;
        }
        line_start = buffer_index + 1U;
    }
    return line_start == buffer_len;
}

static bool logLineContainingIsNotTruncated(const char* buffer_ptr, const char* marker_ptr) {
    const char* marker_match_ptr = strstr(buffer_ptr, marker_ptr);
    if (!marker_match_ptr) {
        return false;
    }
    const char* line_end_ptr = strchr(marker_match_ptr, '\n');
    if (!line_end_ptr) {
        return false;
    }
    const char* truncation_ptr = strstr(marker_match_ptr, "...(TRUNCATED)");
    return !truncation_ptr || truncation_ptr > line_end_ptr;
}

int main(void) {
    int exit_code = EXIT_FAILURE;
    int log_fd = -1;
    DominoLogger logger;
    DominoLogger* logger_ptr = &logger;
    bool logger_initialized_flag = false;
    bool stdout_locked = false;
    char log_path[] = "/tmp/domino-log-safety.XXXXXX";

#define TEST_CHECK(condition, message)                        \
    do {                                                      \
        if (!(condition)) {                                   \
            (void)fprintf(stderr, "FAILED: %s\n", (message)); \
            goto cleanup;                                     \
        }                                                     \
    } while (0)

    TEST_CHECK(dominoLogInit((DominoLogConfig){0}, nullptr, 0U, nullptr) == ERR_NULL_POINTER, "reject null logger storage pointer");
    int init_result = dominoLogInit((DominoLogConfig){.min_level = (DOMINO_LOG_LEVEL)-1}, nullptr, 0U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_INVALID_PARAM && logger.queue_ptr == nullptr,
               "reject negative instance minimum level without allocating logger resources");
    init_result = dominoLogInit((DominoLogConfig){.min_level = (DOMINO_LOG_LEVEL)6}, nullptr, 0U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_INVALID_PARAM && logger.queue_ptr == nullptr,
               "reject instance minimum level above ERROR without allocating logger resources");
    init_result = dominoLogInit((DominoLogConfig){.buffer_size = SIZE_MAX}, nullptr, 0U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_MEMORY_OVERFLOW && logger.queue_ptr == nullptr, "reject overflowing queue capacity before allocating memory");
#if SIZE_MAX > UINT32_MAX
    init_result = dominoLogInit((DominoLogConfig){.buffer_size = (size_t)UINT32_MAX + 1U}, nullptr, 0U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_OUT_OF_RANGE && logger.queue_ptr == nullptr,
               "reject queue capacity beyond the uint32_t limit before allocating memory");
#endif

    init_result = dominoLogInit((DominoLogConfig){0}, nullptr, 1U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_NULL_POINTER && logger.queue_ptr == nullptr && logger.module_count == 0U,
               "reject null module list with nonzero count without allocating logger resources");
    DominoLogModule invalid_module = {.module_id = 1U, .min_level = (DOMINO_LOG_LEVEL)6, .name = "bad-level"};
    init_result = dominoLogInit((DominoLogConfig){0}, &invalid_module, DOMINO_LOG_MAX_MODULES + 1U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_OUT_OF_RANGE && logger.queue_ptr == nullptr && logger.module_count == 0U,
               "reject module count beyond capacity before inspecting the list");
    init_result = dominoLogInit((DominoLogConfig){0}, &invalid_module, 1U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_INVALID_PARAM && logger.queue_ptr == nullptr && logger.module_count == 0U,
               "reject invalid module minimum level without allocating logger resources");
    invalid_module.min_level = DOMINO_LOG_LEVEL_INFO;
    invalid_module.name[0] = '\0';
    init_result = dominoLogInit((DominoLogConfig){0}, &invalid_module, 1U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_INVALID_PARAM && logger.queue_ptr == nullptr && logger.module_count == 0U, "reject empty module name");
    memset(invalid_module.name, 'x', sizeof(invalid_module.name));
    init_result = dominoLogInit((DominoLogConfig){0}, &invalid_module, 1U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_INVALID_PARAM && logger.queue_ptr == nullptr && logger.module_count == 0U, "reject unterminated module name");

    DominoLogModule duplicate_modules[] = {{.module_id = 1U, .name = "first"}, {.module_id = 1U, .name = "second"}};
    init_result = dominoLogInit((DominoLogConfig){0}, duplicate_modules, 2U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_ALREADY_EXISTS && logger.queue_ptr == nullptr && logger.module_count == 0U,
               "reject duplicate module IDs and clear the whole initialization");
    duplicate_modules[1].module_id = 2U;
    memcpy(duplicate_modules[1].name, duplicate_modules[0].name, sizeof(duplicate_modules[1].name));
    init_result = dominoLogInit((DominoLogConfig){0}, duplicate_modules, 2U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_ALREADY_EXISTS && logger.queue_ptr == nullptr && logger.module_count == 0U,
               "reject duplicate module names and clear the whole initialization");
    dominoLogDestroy(nullptr);
    dominoLogDestroy(logger_ptr);

    init_result = dominoLogInit((DominoLogConfig){.buffer_size = 1U}, nullptr, 0U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(logger_initialized_flag && logger.module_count == 0U, "initialize an empty module list after rejected configurations");
    dominoLogDestroy(logger_ptr);
    logger_initialized_flag = false;

    log_fd = mkstemp(log_path);
    TEST_CHECK(log_fd >= 0, "create temporary log file");
    TEST_CHECK(close(log_fd) == 0, "close temporary log file descriptor");
    log_fd = -1;

    DominoLogConfig config = {
        .min_level = DOMINO_LOG_LEVEL_WARN,
        .file_path = log_path,
        .buffer_size = 256U,
    };
    const domino_log_module_id_t module_id = 7U;
    const domino_log_module_id_t inherited_module_id = 8U;
    const domino_log_module_id_t strict_module_id = 9U;
    DominoLogModule modules[] = {
        {.module_id = module_id, .min_level = DOMINO_LOG_LEVEL_DEBUG, .name = "safety"},
        {.module_id = inherited_module_id, .min_level = 0, .name = "inherited"},
        {.module_id = strict_module_id, .min_level = DOMINO_LOG_LEVEL_ERROR, .name = "strict"},
    };
    init_result = dominoLogInit(config, modules, sizeof(modules) / sizeof(modules[0]), logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(logger_initialized_flag, "initialize caller-owned logger and its complete module list");
    memset(modules, 0, sizeof(modules));

    DominoLogSourceLocation static_loc = {.file = "static-source.c", .line = 10, .func = "staticFunction"};
    dominoLog(nullptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "null-instance-formatted");
    logThroughVa(nullptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "null-instance-va");
    dominoLogMsg(nullptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "null-instance-msg");
    dominoLogMsgN(nullptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "null-instance-msgn", strlen("null-instance-msgn"));
    dominoLog(logger_ptr, module_id, DOMINO_LOG_LEVEL_DEBUG, static_loc, "module-debug-%s", "visible");
    dominoLogMsg(logger_ptr, module_id, DOMINO_LOG_LEVEL_TRACE, static_loc, "module-trace-hidden");
    dominoLogMsg(logger_ptr, inherited_module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "inherited-info-hidden");
    dominoLogMsg(logger_ptr, inherited_module_id, DOMINO_LOG_LEVEL_WARN, static_loc, "inherited-warn-visible");
    dominoLogMsg(logger_ptr, strict_module_id, DOMINO_LOG_LEVEL_WARN, static_loc, "strict-warn-hidden");
    dominoLogMsg(logger_ptr, strict_module_id, DOMINO_LOG_LEVEL_ERROR, static_loc, "strict-error-visible");

    dominoLog(logger_ptr, module_id, (DOMINO_LOG_LEVEL)-1, static_loc, "invalid-global-level");
    logThroughVa(logger_ptr, module_id, (DOMINO_LOG_LEVEL)0, static_loc, "invalid-va-level");
    dominoLogMsg(logger_ptr, module_id, (DOMINO_LOG_LEVEL)6, static_loc, "invalid-msg-level");
    dominoLogMsgN(logger_ptr, module_id, (DOMINO_LOG_LEVEL)99, static_loc, "invalid-msgn-level", strlen("invalid-msgn-level"));

    /*
     * 阻塞 writer 的第一条 stdout 写入，使后一条消息直到调用方覆写 source 缓冲后才会格式化。
     * 旧的浅指针队列会输出覆写内容；自包含队列项必须保留调用瞬间的来源位置。
     */
    flockfile(stdout);
    stdout_locked = true;
    dominoLogMsg(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "writer-blocker");

    char owned_file[] = "owned-source.c";
    char owned_func[] = "ownedFunction";
    DominoLogSourceLocation owned_loc = {.file = owned_file, .line = 321, .func = owned_func};
    dominoLogMsg(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, owned_loc, "owned-source-body");
    memset(owned_file, 'x', sizeof(owned_file) - 1U);
    owned_file[sizeof(owned_file) - 1U] = '\0';
    memset(owned_func, 'y', sizeof(owned_func) - 1U);
    owned_func[sizeof(owned_func) - 1U] = '\0';
    /* writer 仍被 stdout 锁阻塞；这批记录超过单次消费上限，必须跨批次完整写出。 */
    for (uint32_t record_index = 0U; record_index < TEST_BATCH_RECORD_COUNT; record_index++) {
        dominoLog(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "drain-batch-%" PRIu32, record_index);
    }
    funlockfile(stdout);
    stdout_locked = false;

    DominoLogSourceLocation null_loc = {.file = nullptr, .line = -8, .func = nullptr};
    dominoLog(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, null_loc, "null-source-%d", 42);
    logThroughVa(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "va-body-%s", "ok");
    dominoLogMsgN(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "msgn-body", strlen("msgn-body"));

    char exact_file[81];
    char exact_func[33];
    memset(exact_file, 'e', sizeof(exact_file) - 1U);
    exact_file[sizeof(exact_file) - 1U] = '\0';
    memset(exact_func, 'h', sizeof(exact_func) - 1U);
    exact_func[sizeof(exact_func) - 1U] = '\0';
    DominoLogSourceLocation exact_loc = {.file = exact_file, .line = 80, .func = exact_func};
    dominoLogMsg(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, exact_loc, "exact-capture-boundary");

    char long_file[1024];
    char long_func[512];
    char long_body[1024];
    memset(long_file, 'f', sizeof(long_file) - 1U);
    long_file[sizeof(long_file) - 1U] = '\0';
    memset(long_func, 'g', sizeof(long_func) - 1U);
    long_func[sizeof(long_func) - 1U] = '\0';
    memset(long_body, 'b', sizeof(long_body));
    DominoLogSourceLocation long_loc = {.file = long_file, .line = 999, .func = long_func};
    dominoLogMsgN(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, long_loc, long_body, sizeof(long_body));

    static const char body_marker[] = "body-only-truncation:";
    memcpy(long_body, body_marker, sizeof(body_marker) - 1U);
    dominoLogMsgN(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, long_body, sizeof(long_body));

    /* 二进制正文放在最后，前面的普通日志仍可使用字符串断言。 */
    static const char binary_body[] = "binary-before\0binary-after";
    dominoLogMsgN(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, binary_body, sizeof(binary_body) - 1U);

    dominoLogDestroy(logger_ptr);
    logger_initialized_flag = false;
    TEST_CHECK(logger.queue_ptr == nullptr, "destroy internal resources while keeping caller-owned storage valid");
    dominoLogDestroy(logger_ptr);

    static char log_buffer[TEST_LOG_BUFFER_SIZE];
    size_t log_len = 0U;
    TEST_CHECK(readLogFile(log_path, log_buffer, sizeof(log_buffer), &log_len), "read generated log file");
    TEST_CHECK(strstr(log_buffer, "null-instance-") == nullptr, "null instance logging never writes to another logger");
    TEST_CHECK(strstr(log_buffer, "module-debug-visible") != nullptr && strstr(log_buffer, "module-trace-hidden") == nullptr,
               "explicit module minimum level overrides the higher instance default");
    TEST_CHECK(strstr(log_buffer, "inherited-warn-visible") != nullptr && strstr(log_buffer, "inherited-info-hidden") == nullptr,
               "zero module minimum level inherits the instance default");
    TEST_CHECK(strstr(log_buffer, "strict-error-visible") != nullptr && strstr(log_buffer, "strict-warn-hidden") == nullptr,
               "explicit module minimum level can be higher than the instance default");
    TEST_CHECK(strstr(log_buffer, "[safety]") != nullptr && strstr(log_buffer, "[inherited]") != nullptr && strstr(log_buffer, "[strict]") != nullptr,
               "initialization copies module IDs, names and thresholds before the caller overwrites the list");
    TEST_CHECK(log_len >= sizeof(binary_body), "binary record contains its complete body and newline");
    size_t binary_body_offset = log_len - sizeof(binary_body);
    TEST_CHECK(memcmp(log_buffer + binary_body_offset, binary_body, sizeof(binary_body) - 1U) == 0 && log_buffer[log_len - 1U] == '\n',
               "explicit-length log preserves embedded NUL and all following body bytes");
    TEST_CHECK(memchr(log_buffer, '\0', binary_body_offset) == nullptr, "ordinary logs and prefixes must not contain a line-buffer terminator");
    TEST_CHECK(logLinesAreBounded(log_buffer, log_len), "every log record must end with newline within the 255-byte output bound");
    TEST_CHECK(strstr(log_buffer, "owned-source.c:321:ownedFunction") != nullptr, "async queue must own the source location captured at call time");
    TEST_CHECK(strstr(log_buffer, "owned-source-body") != nullptr, "owned-source log body is written");
    for (uint32_t record_index = 0U; record_index < TEST_BATCH_RECORD_COUNT; record_index++) {
        char marker_buf[32];
        int marker_len = snprintf(marker_buf, sizeof(marker_buf), "drain-batch-%" PRIu32 "\n", record_index);
        TEST_CHECK(marker_len > 0 && (size_t)marker_len < sizeof(marker_buf), "format expected drain marker");
        const char* marker_ptr = strstr(log_buffer, marker_buf);
        TEST_CHECK(marker_ptr != nullptr && strstr(marker_ptr + (size_t)marker_len, marker_buf) == nullptr,
                   "writer outputs every record exactly once across batches");
    }
    TEST_CHECK(strstr(log_buffer, "?:0:?") != nullptr && strstr(log_buffer, "null-source-42") != nullptr,
               "null source location is normalized safely");
    TEST_CHECK(strstr(log_buffer, "va-body-ok") != nullptr && strstr(log_buffer, "msgn-body") != nullptr,
               "formatted and explicit-length entry paths both write valid records");
    TEST_CHECK(logLineContainingIsNotTruncated(log_buffer, "exact-capture-boundary"),
               "source strings exactly at the capture limits must not be marked truncated");
    TEST_CHECK(strstr(log_buffer, "...(TRUNCATED)") != nullptr, "long source and body use the bounded truncation suffix");
    const char* body_match_ptr = strstr(log_buffer, body_marker);
    TEST_CHECK(body_match_ptr != nullptr, "body-only truncation retains the start of the body");
    const char* body_line_start_ptr = body_match_ptr;
    while (body_line_start_ptr > log_buffer && body_line_start_ptr[-1] != '\n') {
        body_line_start_ptr--;
    }
    const char* body_line_end_ptr = strchr(body_match_ptr, '\n');
    const char* body_suffix_ptr = strstr(body_match_ptr, "...(TRUNCATED)\n");
    TEST_CHECK(body_line_end_ptr != nullptr && body_suffix_ptr != nullptr && body_suffix_ptr < body_line_end_ptr &&
                   (size_t)(body_line_end_ptr - body_line_start_ptr) + 1U == TEST_LOG_LINE_MAX,
               "long body with short source fills the output bound and ends with truncation suffix and newline");
    TEST_CHECK(strstr(log_buffer, "invalid-global-level") == nullptr && strstr(log_buffer, "invalid-va-level") == nullptr &&
                   strstr(log_buffer, "invalid-msg-level") == nullptr && strstr(log_buffer, "invalid-msgn-level") == nullptr,
               "all public entry paths discard invalid levels");

    size_t initial_log_len = log_len;
    const DominoLogModule module = {.module_id = module_id, .min_level = DOMINO_LOG_LEVEL_DEBUG, .name = "safety"};
    init_result = dominoLogInit(config, &module, 1U, logger_ptr);
    logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(logger_initialized_flag && logger.module_count == 1U, "reinitialize the same caller-owned storage with a new module list");
    dominoLogMsg(logger_ptr, module_id, DOMINO_LOG_LEVEL_INFO, static_loc, "same-storage-reinitialized");
    dominoLogDestroy(logger_ptr);
    logger_initialized_flag = false;
    TEST_CHECK(logger.queue_ptr == nullptr, "reinitialized storage releases its resources after destruction");
    TEST_CHECK(readLogFile(log_path, log_buffer, sizeof(log_buffer), &log_len) && log_len > initial_log_len,
               "reinitialized caller-owned logger appends a new record");
    TEST_CHECK(
        strstr(log_buffer + initial_log_len, "[seq=1]") != nullptr && strstr(log_buffer + initial_log_len, "same-storage-reinitialized") != nullptr,
        "same storage writes logs with a fresh per-instance sequence after reinitialization");

    exit_code = EXIT_SUCCESS;

cleanup:
    if (stdout_locked) {
        funlockfile(stdout);
    }
    if (logger_initialized_flag) {
        dominoLogDestroy(logger_ptr);
    }
    if (log_fd >= 0) {
        (void)close(log_fd);
    }
    (void)unlink(log_path);
    return exit_code;
}
