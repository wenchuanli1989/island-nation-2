#include "entry.h"

#include "../entry.h"
#include "../human/entry.h"
DOMINO_CODE dominoDispatchMessage(const DominoEngineMessage* message) {
    if (message->type == DOMINO_ENGINE_MSG_NONE)
        return CODE_OK;
    const DominoNpcRequest* request = &message->payload.npc;
    if (request->session_id != dominoEngineClientSessionID())
        return ERR_GAME_STATE_INVALID;
    uint64_t client_id = g_domino_engine_launch_config.client_id ? g_domino_engine_launch_config.client_id : 1;
    if (message->type == DOMINO_ENGINE_MSG_QUERY_NPC) {
        DominoEngineMessage response = {.type = DOMINO_ENGINE_MSG_NPC_RESULT};
        dominoHumanQueryNpc(client_id, request, &response.payload.result);
        dominoLoopProduceToMain(&response);
    } else if (message->type == DOMINO_ENGINE_MSG_SET_NPC_NAME)
        dominoHumanRequestNameChange(client_id, request);
    else
        return ERR_INVALID_MESSAGE;
    return CODE_OK;
}
