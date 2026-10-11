#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "thread_message_test_helpers.h"
#include "time/entry.h"

#define TEST_CHECK(condition, message)                              \
    do {                                                            \
        if (!(condition)) {                                         \
            (void)fprintf(stderr, "%s: %s\n", __func__, (message)); \
            goto cleanup;                                           \
        }                                                           \
    } while (0)

typedef struct TestPlan {
    uint64_t request_id;
    uint32_t human_id;
} TestPlan;

/** @brief 载荷独立于发送栈；满载失败仍归发送者，成功入队后由接收者释放。 */
static bool testPayloadOwnership(void) {
    bool success = false;
    DominoThreadQueue inbox = {0};
    DominoThreadMessage outgoing = {0};
    DominoThreadMessage incoming = {0};
    TestPlan plan = {.request_id = 97U, .human_id = 12U};
    uint32_t command = 43U;
    TEST_CHECK(dominoThreadQueueInit(&inbox, sizeof(DominoThreadMessage), 1U, 1U) == CODE_OK, "initialize one-slot inbox");
    TEST_CHECK(dominoThreadMessageInitCopy(&outgoing, DOMINO_THREAD_MESSAGE_NONE, &plan, sizeof(plan)) == ERR_INVALID_PARAM &&
                   dominoThreadMessageInitCopy(&outgoing, DOMINO_THREAD_MESSAGE_AI_RESULT, &plan, 0U) == ERR_INVALID_PARAM,
               "reject empty message type and empty payload");
    TEST_CHECK(dominoThreadMessageInitCopy(&outgoing, DOMINO_THREAD_MESSAGE_AI_RESULT, &plan, sizeof(plan)) == CODE_OK,
               "copy a plan into an independent block");
    plan.request_id = 1000U;
    TEST_CHECK(dominoThreadQueueProduce(&inbox, &outgoing, false) == CODE_OK, "transfer first payload to inbox");
    outgoing = (DominoThreadMessage){0};
    TEST_CHECK(dominoThreadMessageInitCopy(&outgoing, DOMINO_THREAD_MESSAGE_COMMAND, &command, sizeof(command)) == CODE_OK,
               "prepare another message type");
    void* retained_ptr = outgoing.items_ptr;
    TEST_CHECK(dominoThreadQueueProduce(&inbox, &outgoing, false) == ERR_QUEUE_FULL && outgoing.items_ptr == retained_ptr && outgoing.id == 0U &&
                   *(const uint32_t*)outgoing.items_ptr == command,
               "full queue retains sender ownership and payload");
    TEST_CHECK(dominoThreadQueueConsume(&inbox, &incoming, false) == CODE_OK && incoming.items_type == DOMINO_THREAD_MESSAGE_AI_RESULT &&
                   incoming.items_size == sizeof(TestPlan) && ((const TestPlan*)incoming.items_ptr)->request_id == 97U,
               "receiver sees original plan after sender stack changes");
    dominoThreadMessageRelease(&incoming);
    TEST_CHECK(dominoThreadQueueProduce(&inbox, &outgoing, false) == CODE_OK, "same inbox accepts command after AI result");
    uint64_t command_message_id = outgoing.id;
    outgoing = (DominoThreadMessage){0};
    TEST_CHECK(command_message_id == 2U, "message IDs increase across payload types");
    TEST_CHECK(dominoThreadQueueConsume(&inbox, &incoming, false) == CODE_OK && incoming.items_type == DOMINO_THREAD_MESSAGE_COMMAND &&
                   incoming.items_size == sizeof(command) && *(const uint32_t*)incoming.items_ptr == command,
               "receiver selects a different payload shape using items_type");
    dominoThreadMessageRelease(&incoming);
    TEST_CHECK(incoming.items_ptr == nullptr && incoming.items_type == DOMINO_THREAD_MESSAGE_NONE, "release clears the descriptor");
    success = true;

cleanup:
    dominoThreadMessageRelease(&outgoing);
    dominoThreadMessageRelease(&incoming);
    testDestroyMessageQueue(&inbox);
    return success;
}

/** @brief 一个收件队列接收 AI、真实 timer、命令；取消只解释 TIMER 载荷，保持其他消息及 FIFO。 */
static bool testMixedInbox(void) {
    bool success = false;
    DominoThreadQueue inbox = {0};
    DominoThreadMessage outgoing = {0};
    DominoThreadMessage received[3] = {0};
    uint32_t received_count = 0U;
    TestPlan plan = {.request_id = 29U, .human_id = 7U};
    uint32_t command = 81U;
    dominoTimeModuleInitBefore();
    dominoTimeModuleInitAfter();
    TEST_CHECK(dominoThreadQueueInit(&inbox, sizeof(DominoThreadMessage), 2U, 4U) == CODE_OK && dominoTimerModuleInit() == CODE_OK,
               "initialize a shared business inbox and timer");
    TEST_CHECK(dominoThreadMessageInitCopy(&outgoing, DOMINO_THREAD_MESSAGE_AI_RESULT, &plan, sizeof(plan)) == CODE_OK &&
                   dominoThreadQueueProduce(&inbox, &outgoing, false) == CODE_OK,
               "planner posts an AI result");
    outgoing = (DominoThreadMessage){0};
    uint64_t epoch;
    TEST_CHECK(dominoThreadQueueSnapshot(&inbox, &epoch) == CODE_OK, "snapshot before timer delivery");
    DominoTimer request = {.event_queue_ptr = &inbox, .event_type = 3U};
    TEST_CHECK(dominoTimerSchedule(&request) == CODE_OK, "timer accepts the same inbox containing an AI result");
    struct timespec deadline;
    TEST_CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC, "read test deadline");
    deadline.tv_sec += 5;
    TEST_CHECK(dominoThreadQueueWait(&inbox, epoch, &deadline) == CODE_OK, "wait for timer message");
    uint64_t delivered_epoch;
    TEST_CHECK(dominoThreadQueueSnapshot(&inbox, &delivered_epoch) == CODE_OK && delivered_epoch != epoch, "timer delivery notifies the inbox");
    TEST_CHECK(dominoThreadMessageInitCopy(&outgoing, DOMINO_THREAD_MESSAGE_COMMAND, &command, sizeof(command)) == CODE_OK &&
                   dominoThreadQueueProduce(&inbox, &outgoing, false) == CODE_OK,
               "another sender posts a command and grows the same queue");
    outgoing = (DominoThreadMessage){0};
    TEST_CHECK(dominoTimerCancel(request.timer_id, &inbox) == CODE_OK, "cancel scans past AI data to mark only the timer payload");
    TEST_CHECK(dominoTimerCancel(request.timer_id, &inbox) == ERR_NOT_FOUND, "repeat cancellation remains not found");
    TEST_CHECK(dominoThreadQueueConsumeBatch(&inbox, received, 3U, &received_count, false) == CODE_OK && received_count == 3U,
               "executor consumes all three types in one batch");
    TEST_CHECK(received[0].id == 1U && received[1].id == 2U && received[2].id == 3U && received[0].items_type == DOMINO_THREAD_MESSAGE_AI_RESULT &&
                   received[0].items_size == sizeof(TestPlan) && received[1].items_type == DOMINO_THREAD_MESSAGE_TIMER &&
                   received[1].items_size == sizeof(DominoTimer) && received[2].items_type == DOMINO_THREAD_MESSAGE_COMMAND &&
                   received[2].items_size == sizeof(command),
               "FIFO, payload types and sizes survive growth and cancellation");
    const DominoTimer* timer_ptr = received[1].items_ptr;
    TEST_CHECK(timer_ptr->timer_id == request.timer_id && timer_ptr->id == request.id && timer_ptr->cancelled_flag &&
                   ((const TestPlan*)received[0].items_ptr)->request_id == plan.request_id && *(const uint32_t*)received[2].items_ptr == command,
               "only the timer is marked; other payloads and business IDs are unchanged");
    TEST_CHECK(dominoTimerCancel(request.timer_id, &inbox) == ERR_NOT_FOUND, "dequeued payloads are no longer owned by the queue");
    for (uint32_t i = 0U; i < received_count; ++i) {
        dominoThreadMessageRelease(&received[i]);
    }
    received_count = 0U;
    TEST_CHECK(dominoThreadMessageInitCopy(&outgoing, DOMINO_THREAD_MESSAGE_TIMER, &command, sizeof(command)) == CODE_OK &&
                   dominoThreadQueueProduce(&inbox, &outgoing, false) == CODE_OK,
               "inject a malformed timer payload");
    outgoing = (DominoThreadMessage){0};
    TEST_CHECK(dominoTimerCancel(request.timer_id, &inbox) == ERR_INVALID_MESSAGE, "validate payload size before interpreting it as DominoTimer");
    success = true;

cleanup:
    dominoTimerModuleExit();
    dominoTimeModuleExit();
    dominoThreadMessageRelease(&outgoing);
    for (uint32_t i = 0U; i < received_count; ++i) {
        dominoThreadMessageRelease(&received[i]);
    }
    testDestroyMessageQueue(&inbox);
    return success;
}

int main(void) {
    return testPayloadOwnership() && testMixedInbox() ? EXIT_SUCCESS : EXIT_FAILURE;
}
