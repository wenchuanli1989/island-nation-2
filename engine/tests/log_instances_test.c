#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "domino_engine_log.h"
#include "domino_shared_error_codes.h"

#define TEST_WORKER_COUNT 4U
#define TEST_RECORDS_PER_WORKER 24U
#define TEST_LOG_BUFFER_SIZE (64U * 1024U)
#define TEST_MAX_RECORD_COUNT (2U * TEST_WORKER_COUNT * TEST_RECORDS_PER_WORKER + 5U)

typedef struct {
    DominoLogger* logger_a_ptr;
    DominoLogger* logger_b_ptr;
    const char* phase_ptr;
    uint32_t worker_index;
} LogWorkerContext;

static const DominoLogSourceLocation g_source = {.file = "instances.c", .line = 10, .func = "produceLogs"};

static int produceLogs(void* context_ptr) {
    const LogWorkerContext* worker_ptr = context_ptr;
    for (uint32_t record_index = 0U; record_index < TEST_RECORDS_PER_WORKER; record_index++) {
        if (worker_ptr->logger_a_ptr) {
            dominoLog(worker_ptr->logger_a_ptr, 2U, DOMINO_LOG_LEVEL_DEBUG, g_source, "A|%s|%" PRIu32 "|%" PRIu32, worker_ptr->phase_ptr,
                      worker_ptr->worker_index, record_index);
        }
        dominoLog(worker_ptr->logger_b_ptr, 2U, DOMINO_LOG_LEVEL_INFO, g_source, "B|%s|%" PRIu32 "|%" PRIu32, worker_ptr->phase_ptr,
                  worker_ptr->worker_index, record_index);
    }
    return 0;
}

static bool readLogFile(const char* path_ptr, char* buffer_ptr, size_t buffer_size) {
    FILE* file_ptr = fopen(path_ptr, "rb");
    if (!file_ptr) {
        return false;
    }
    size_t read_len = fread(buffer_ptr, 1U, buffer_size - 1U, file_ptr);
    bool success = !ferror(file_ptr) && feof(file_ptr) && memchr(buffer_ptr, '\0', read_len) == nullptr;
    buffer_ptr[read_len] = '\0';
    success = fclose(file_ptr) == 0 && success;
    return success;
}

static bool recordOccursOnce(const char* buffer_ptr, const char* marker_ptr) {
    const char* match_ptr = strstr(buffer_ptr, marker_ptr);
    return match_ptr && strstr(match_ptr + strlen(marker_ptr), marker_ptr) == nullptr;
}

static bool workerRecordsAreComplete(const char* buffer_ptr, char instance_name, const char* phase_ptr) {
    for (uint32_t worker_index = 0U; worker_index < TEST_WORKER_COUNT; worker_index++) {
        for (uint32_t record_index = 0U; record_index < TEST_RECORDS_PER_WORKER; record_index++) {
            char marker_buf[80];
            int marker_len =
                snprintf(marker_buf, sizeof(marker_buf), "%c|%s|%" PRIu32 "|%" PRIu32 "\n", instance_name, phase_ptr, worker_index, record_index);
            if (marker_len < 0 || (size_t)marker_len >= sizeof(marker_buf) || !recordOccursOnce(buffer_ptr, marker_buf)) {
                return false;
            }
        }
    }
    return true;
}

static bool recordSequencesAreComplete(const char* buffer_ptr, size_t expected_count) {
    if (expected_count > TEST_MAX_RECORD_COUNT) {
        return false;
    }
    bool sequence_seen[TEST_MAX_RECORD_COUNT + 1U] = {false};
    size_t record_count = 0U;
    const char* line_ptr = buffer_ptr;
    while (*line_ptr) {
        const char* line_end_ptr = strchr(line_ptr, '\n');
        const char* sequence_ptr = strstr(line_ptr, "[seq=");
        if (!line_end_ptr || !sequence_ptr || sequence_ptr >= line_end_ptr) {
            return false;
        }
        char* sequence_end_ptr = nullptr;
        unsigned long sequence_num = strtoul(sequence_ptr + 5U, &sequence_end_ptr, 10);
        if (sequence_end_ptr == sequence_ptr + 5U || sequence_end_ptr >= line_end_ptr || *sequence_end_ptr != ']' || sequence_num == 0U ||
            sequence_num > expected_count || sequence_seen[sequence_num]) {
            return false;
        }
        if (record_count == 0U && sequence_num != 1U) {
            return false;
        }
        sequence_seen[sequence_num] = true;
        record_count++;
        line_ptr = line_end_ptr + 1U;
    }
    return record_count == expected_count;
}

int main(void) {
    int exit_code = EXIT_FAILURE;
    int log_fd_a = -1;
    int log_fd_b = -1;
    DominoLogger logger_a;
    DominoLogger logger_b;
    DominoLogger failed_logger;
    DominoLogger* logger_a_ptr = &logger_a;
    DominoLogger* logger_b_ptr = &logger_b;
    bool logger_a_initialized_flag = false;
    bool logger_b_initialized_flag = false;
    bool failed_logger_initialized_flag = false;
    char log_path_a[] = "/tmp/domino-log-instances-a.XXXXXX";
    char log_path_b[] = "/tmp/domino-log-instances-b.XXXXXX";
    thrd_t worker_threads[TEST_WORKER_COUNT];
    LogWorkerContext worker_contexts[TEST_WORKER_COUNT];
    size_t started_count = 0U;
    size_t joined_count = 0U;

#define TEST_CHECK(condition, message)                        \
    do {                                                      \
        if (!(condition)) {                                   \
            (void)fprintf(stderr, "FAILED: %s\n", (message)); \
            goto cleanup;                                     \
        }                                                     \
    } while (0)

    log_fd_a = mkstemp(log_path_a);
    TEST_CHECK(log_fd_a >= 0, "create instance A log file");
    TEST_CHECK(close(log_fd_a) == 0, "close instance A temporary descriptor");
    log_fd_a = -1;
    log_fd_b = mkstemp(log_path_b);
    TEST_CHECK(log_fd_b >= 0, "create instance B log file");
    TEST_CHECK(close(log_fd_b) == 0, "close instance B temporary descriptor");
    log_fd_b = -1;

    const DominoLogModule modules_a[] = {
        {.module_id = 1U, .min_level = 0, .name = "common"},
        {.module_id = 2U, .min_level = DOMINO_LOG_LEVEL_DEBUG, .name = "a-debug"},
        {.module_id = 3U, .min_level = DOMINO_LOG_LEVEL_ERROR, .name = "a-strict"},
    };
    const DominoLogModule modules_b[] = {
        {.module_id = 1U, .min_level = 0, .name = "common"},
        {.module_id = 2U, .min_level = DOMINO_LOG_LEVEL_INFO, .name = "b-info"},
        {.module_id = 4U, .min_level = DOMINO_LOG_LEVEL_WARN, .name = "b-warn"},
    };
    int init_result = dominoLogInit((DominoLogConfig){.file_path = log_path_a, .buffer_size = 1U, .min_level = DOMINO_LOG_LEVEL_WARN}, modules_a,
                                    sizeof(modules_a) / sizeof(modules_a[0]), logger_a_ptr);
    logger_a_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(logger_a_initialized_flag, "initialize caller-owned instance A with its module list and a one-element queue");
    init_result = dominoLogInit((DominoLogConfig){.file_path = log_path_b, .buffer_size = 3U, .min_level = DOMINO_LOG_LEVEL_ERROR}, modules_b,
                                sizeof(modules_b) / sizeof(modules_b[0]), logger_b_ptr);
    logger_b_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(logger_b_initialized_flag, "initialize B with independent module thresholds while A is alive and reuses IDs and names");
    TEST_CHECK(logger_a_ptr != logger_b_ptr, "instances use distinct caller-owned storage");

    dominoLogMsg(logger_a_ptr, 1U, DOMINO_LOG_LEVEL_INFO, g_source, "A|default-info-hidden");
    dominoLogMsg(logger_a_ptr, 1U, DOMINO_LOG_LEVEL_WARN, g_source, "A|default-warn");
    dominoLogMsg(logger_a_ptr, 2U, DOMINO_LOG_LEVEL_TRACE, g_source, "A|explicit-trace-hidden");
    dominoLogMsg(logger_a_ptr, 2U, DOMINO_LOG_LEVEL_DEBUG, g_source, "A|explicit-debug");
    dominoLogMsg(logger_a_ptr, 3U, DOMINO_LOG_LEVEL_WARN, g_source, "A|strict-warn-hidden");
    dominoLogMsg(logger_a_ptr, 3U, DOMINO_LOG_LEVEL_ERROR, g_source, "A|strict-error");
    dominoLogMsg(logger_a_ptr, 4U, DOMINO_LOG_LEVEL_ERROR, g_source, "A|unregistered-hidden");

    dominoLogMsg(logger_b_ptr, 1U, DOMINO_LOG_LEVEL_WARN, g_source, "B|default-warn-hidden");
    dominoLogMsg(logger_b_ptr, 1U, DOMINO_LOG_LEVEL_ERROR, g_source, "B|default-error");
    dominoLogMsg(logger_b_ptr, 2U, DOMINO_LOG_LEVEL_DEBUG, g_source, "B|explicit-debug-hidden");
    dominoLogMsg(logger_b_ptr, 2U, DOMINO_LOG_LEVEL_INFO, g_source, "B|explicit-info");
    dominoLogMsg(logger_b_ptr, 4U, DOMINO_LOG_LEVEL_WARN, g_source, "B|explicit-warn");
    dominoLogMsg(logger_b_ptr, 3U, DOMINO_LOG_LEVEL_ERROR, g_source, "B|unregistered-hidden");

    while (started_count < TEST_WORKER_COUNT) {
        worker_contexts[started_count] = (LogWorkerContext){
            .logger_a_ptr = logger_a_ptr, .logger_b_ptr = logger_b_ptr, .phase_ptr = "shared", .worker_index = (uint32_t)started_count};
        TEST_CHECK(thrd_create(&worker_threads[started_count], produceLogs, &worker_contexts[started_count]) == thrd_success,
                   "start concurrent producers for both instances");
        started_count++;
    }
    while (joined_count < started_count) {
        int worker_result = -1;
        TEST_CHECK(thrd_join(worker_threads[joined_count], &worker_result) == thrd_success, "join producers using instance A");
        joined_count++;
        TEST_CHECK(worker_result == 0, "both-instance producer completed successfully");
    }

    /* A 的所有生产者均已回收；新一批线程仅写 B，可与 A 的销毁安全并发。 */
    started_count = 0U;
    joined_count = 0U;
    while (started_count < TEST_WORKER_COUNT) {
        worker_contexts[started_count] =
            (LogWorkerContext){.logger_b_ptr = logger_b_ptr, .phase_ptr = "survivor", .worker_index = (uint32_t)started_count};
        TEST_CHECK(thrd_create(&worker_threads[started_count], produceLogs, &worker_contexts[started_count]) == thrd_success,
                   "start producers that keep using instance B during A destruction");
        started_count++;
    }
    dominoLogDestroy(logger_a_ptr);
    logger_a_initialized_flag = false;
    TEST_CHECK(logger_a.queue_ptr == nullptr, "destroy instance A resources while retaining caller-owned storage");
    dominoLogMsg(logger_b_ptr, 2U, DOMINO_LOG_LEVEL_INFO, g_source, "B|after-a-destroy");

    /* 普通文件不能作为目录使用，确保 fopen 失败不依赖权限或机器上的目录布局。 */
    char invalid_path[sizeof(log_path_a) + sizeof("/child.log")];
    int invalid_path_len = snprintf(invalid_path, sizeof(invalid_path), "%s/child.log", log_path_a);
    TEST_CHECK(invalid_path_len > 0 && (size_t)invalid_path_len < sizeof(invalid_path), "prepare deterministic file-open failure");
    init_result = dominoLogInit((DominoLogConfig){.file_path = invalid_path}, modules_a, sizeof(modules_a) / sizeof(modules_a[0]), &failed_logger);
    failed_logger_initialized_flag = init_result == CODE_OK;
    TEST_CHECK(init_result == ERR_FILE_OPEN && failed_logger.queue_ptr == nullptr && failed_logger.module_count == 0U,
               "failed initialization reports file-open failure and releases internal resources");
    dominoLogDestroy(&failed_logger);
    dominoLogMsg(logger_b_ptr, 2U, DOMINO_LOG_LEVEL_INFO, g_source, "B|after-failed-init");
    while (joined_count < started_count) {
        int worker_result = -1;
        TEST_CHECK(thrd_join(worker_threads[joined_count], &worker_result) == thrd_success, "join remaining instance B producers");
        joined_count++;
        TEST_CHECK(worker_result == 0, "surviving-instance producer completed successfully");
    }
    dominoLogDestroy(logger_b_ptr);
    logger_b_initialized_flag = false;
    TEST_CHECK(logger_b.queue_ptr == nullptr, "destroy instance B resources while retaining caller-owned storage");

    static char log_buffer_a[TEST_LOG_BUFFER_SIZE];
    static char log_buffer_b[TEST_LOG_BUFFER_SIZE];
    TEST_CHECK(readLogFile(log_path_a, log_buffer_a, sizeof(log_buffer_a)) && readLogFile(log_path_b, log_buffer_b, sizeof(log_buffer_b)),
               "read both flushed instance files");
    TEST_CHECK(recordSequencesAreComplete(log_buffer_a, TEST_WORKER_COUNT * TEST_RECORDS_PER_WORKER + 3U) &&
                   recordSequencesAreComplete(log_buffer_b, TEST_MAX_RECORD_COUNT),
               "each instance starts its own sequence at one and writes every record exactly once");
    TEST_CHECK(workerRecordsAreComplete(log_buffer_a, 'A', "shared") && workerRecordsAreComplete(log_buffer_b, 'B', "shared") &&
                   workerRecordsAreComplete(log_buffer_b, 'B', "survivor"),
               "all concurrent producer markers appear exactly once in the correct file");
    TEST_CHECK(strstr(log_buffer_a, "B|") == nullptr && strstr(log_buffer_b, "A|") == nullptr && strstr(log_buffer_a, "[b-info]") == nullptr &&
                   strstr(log_buffer_b, "[a-debug]") == nullptr,
               "instance files and module registries are isolated");
    TEST_CHECK(strstr(log_buffer_a, "[a-debug]") != nullptr && strstr(log_buffer_b, "[b-info]") != nullptr,
               "each instance uses its own name for the shared module ID");
    TEST_CHECK(strstr(log_buffer_a, "hidden") == nullptr && strstr(log_buffer_b, "hidden") == nullptr,
               "independent module thresholds and registration reject all filtered records");
    TEST_CHECK(recordOccursOnce(log_buffer_a, "A|default-warn\n") && recordOccursOnce(log_buffer_a, "A|explicit-debug\n") &&
                   recordOccursOnce(log_buffer_a, "A|strict-error\n") && recordOccursOnce(log_buffer_b, "B|default-error\n") &&
                   recordOccursOnce(log_buffer_b, "B|explicit-info\n") && recordOccursOnce(log_buffer_b, "B|explicit-warn\n"),
               "default inheritance and explicit thresholds are evaluated independently per instance");
    TEST_CHECK(recordOccursOnce(log_buffer_b, "B|after-a-destroy\n") && recordOccursOnce(log_buffer_b, "B|after-failed-init\n"),
               "instance B remains usable after destroying A and after another instance fails to initialize");
    exit_code = EXIT_SUCCESS;

cleanup:
    while (joined_count < started_count) {
        (void)thrd_join(worker_threads[joined_count], nullptr);
        joined_count++;
    }
    if (failed_logger_initialized_flag) {
        dominoLogDestroy(&failed_logger);
    }
    if (logger_a_initialized_flag) {
        dominoLogDestroy(logger_a_ptr);
    }
    if (logger_b_initialized_flag) {
        dominoLogDestroy(logger_b_ptr);
    }
    if (log_fd_a >= 0) {
        (void)close(log_fd_a);
    }
    if (log_fd_b >= 0) {
        (void)close(log_fd_b);
    }
    (void)unlink(log_path_a);
    (void)unlink(log_path_b);
    return exit_code;
}
