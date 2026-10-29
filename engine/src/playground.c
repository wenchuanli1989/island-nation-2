#include "domino_engine.h"
#include "domino_shared_common.h"
#include "domino_shared_nav.h"
#include "nav/entry.h"
#include "nav/path.h"
#include "nav/state.h"

int dominoEnginePlayground(int argc, char* argv[]) {
    dominoNavModuleInit();
    dominoNavModuleInitAfter();
    DOMINO_CODE result = CODE_OK;

    DominoForkRoad fork_road_0 = {.num = {"0"}};
    fork_road_0.position_x = 0;
    fork_road_0.position_y = 0;
    DominoForkRoad fork_road_1 = {.num = {"1"}};
    fork_road_1.position_x = 100;
    fork_road_1.position_y = 0;
    result = dominoAddRoad(&fork_road_0, &fork_road_1, 1, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 0 -> 1 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }

    DominoForkRoad fork_road_900 = {.num = {"9"}};
    fork_road_900.position_x = 50;
    fork_road_900.position_y = -100;
    result = dominoAddRoad(&fork_road_0, &fork_road_900, 1, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 0 -> 900 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }

    DominoForkRoad fork_road_5 = {.num = {"5"}};
    fork_road_5.position_x = 200;
    fork_road_5.position_y = -100;
    result = dominoAddRoad(&fork_road_900, &fork_road_5, 1, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 900 -> 5 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }

    DominoForkRoad fork_road_2 = {.num = {"2"}};
    fork_road_2.position_x = 200;
    fork_road_2.position_y = 0;
    result = dominoAddRoad(&fork_road_1, &fork_road_2, 2, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 1 -> 2 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }
    result = dominoAddRoad(&fork_road_1, &fork_road_5, 20, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 1 -> 5 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }

    DominoForkRoad fork_road_3 = {.num = {"3"}};
    fork_road_3.position_x = 300;
    fork_road_3.position_y = 0;
    result = dominoAddRoad(&fork_road_2, &fork_road_3, 3, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 2 -> 3 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }
    result = dominoAddRoad(&fork_road_2, &fork_road_5, 100, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 2 -> 5 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }

    DominoForkRoad fork_road_4 = {.num = {"4"}};
    fork_road_4.position_x = 300;
    fork_road_4.position_y = -100;
    result = dominoAddRoad(&fork_road_3, &fork_road_4, 5, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 3 -> 4 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }
    result = dominoAddRoad(&fork_road_3, &fork_road_5, 5, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 3 -> 5 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }

    result = dominoAddRoad(&fork_road_4, &fork_road_5, 6, 0, 0);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("add road 4 -> 5 failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }

    FloydPathPlanningResult path_planning_result = {0};
    result = dominoNavPathPlanning(0, 0, &path_planning_result);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("navPathPlanning failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }
    DOMINO_PRINT_MESSAGE("path_planning_result: %ld", path_planning_result.total_tick);

    DominoRoadLine road_line = {0};
    road_line.current_location = (DominoLocationXY){.x = INT32_MIN, .y = INT32_MIN};
    road_line.region_index = 0;
    road_line.type = 0;
    road_line.start_location = (DominoLocationXY){.x = -10, .y = -10};
    road_line.target_location = (DominoLocationXY){.x = 180, .y = -90};
    result = dominoNavigation(&road_line);
    if (result != CODE_OK) {
        DOMINO_PRINT_MESSAGE("navigation failed: %s (code=%d)", dominoErrorCodeToString(result), (int)result);
        goto cleanup;
    }
    for (int i = 0; i < road_line.fork_road_count; i++) {
        DominoForkRoad* fork_road = dominoGetForkRoadByID(road_line.fork_road_id_list[i]);
        DOMINO_PRINT_MESSAGE("fork_road_num: %s", fork_road->num);
    }

cleanup:
    dominoNavModuleDestroyBefore();
    dominoNavModuleDestroy();
    return result == CODE_OK ? 0 : 1;
}
