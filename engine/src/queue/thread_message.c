#include "thread_message.h"

#include <stdlib.h>
#include <string.h>

#include "../logger/error.h"

DOMINO_CODE dominoThreadMessageInitCopy(DominoThreadMessage* message_ptr, DominoThreadMessageType items_type, const void* payload_ptr,
                                        size_t items_size) {
    if (message_ptr == nullptr || payload_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_NULL_POINTER, "message copy requires a message and payload");
    }
    if (items_type <= DOMINO_THREAD_MESSAGE_NONE || items_type > DOMINO_THREAD_MESSAGE_COMMAND || items_size == 0U) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_INVALID_PARAM, "message copy requires a payload type and nonzero size");
    }
    void* items_ptr = malloc(items_size);
    if (items_ptr == nullptr) {
        return DOMINO_LOG_DIRECT_ERROR(ERR_MEMORY_ALLOC, "message payload allocation failed");
    }
    memcpy(items_ptr, payload_ptr, items_size);
    *message_ptr = (DominoThreadMessage){.items_ptr = items_ptr, .items_size = items_size, .items_type = items_type};
    return CODE_OK;
}

void dominoThreadMessageRelease(DominoThreadMessage* message_ptr) {
    if (message_ptr != nullptr) {
        free(message_ptr->items_ptr);
        *message_ptr = (DominoThreadMessage){0};
    }
}
