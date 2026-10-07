
#include <stdio.h>

#include "domino_engine.h"
#include "domino_shared_nav.h"
#include "nav/entry.h"
#include "nav/path.h"
#include "nav/state.h"

int dominoEnginePlayground(int argc, char* argv[]) {
    DOMINO_CODE init_result = dominoNavModuleInitBefore();
    if (init_result != CODE_OK) {
        return init_result;
    }

    DominoForkRoad fork_road_0 = {.num = {"0"}};
    fork_road_0.position_x = 0;
    fork_road_0.position_y = 0;
    DominoForkRoad fork_road_1 = {.num = {"1"}};
    fork_road_1.position_x = 100;
    fork_road_1.position_y = 0;
    dominoAddRoad(&fork_road_0, &fork_road_1, 1, 0, 0);

    DominoForkRoad fork_road_900 = {.num = {"9"}};
    fork_road_900.position_x = 50;
    fork_road_900.position_y = -100;
    dominoAddRoad(&fork_road_0, &fork_road_900, 1, 0, 0);

    DominoForkRoad fork_road_5 = {.num = {"5"}};
    fork_road_5.position_x = 200;
    fork_road_5.position_y = -100;
    dominoAddRoad(&fork_road_900, &fork_road_5, 1, 0, 0);

    DominoForkRoad fork_road_2 = {.num = {"2"}};
    fork_road_2.position_x = 200;
    fork_road_2.position_y = 0;
    dominoAddRoad(&fork_road_1, &fork_road_2, 2, 0, 0);
    dominoAddRoad(&fork_road_1, &fork_road_5, 20, 0, 0);

    DominoForkRoad fork_road_3 = {.num = {"3"}};
    fork_road_3.position_x = 300;
    fork_road_3.position_y = 0;
    dominoAddRoad(&fork_road_2, &fork_road_3, 3, 0, 0);
    dominoAddRoad(&fork_road_2, &fork_road_5, 100, 0, 0);

    DominoForkRoad fork_road_4 = {.num = {"4"}};
    fork_road_4.position_x = 300;
    fork_road_4.position_y = -100;
    dominoAddRoad(&fork_road_3, &fork_road_4, 5, 0, 0);
    dominoAddRoad(&fork_road_3, &fork_road_5, 5, 0, 0);

    dominoAddRoad(&fork_road_4, &fork_road_5, 6, 0, 0);

    FloydPathPlanningResult path_planning_result = {0};
    DOMINO_CODE err = dominoNavPathPlanning(0, 0, &path_planning_result);
    if (err != CODE_OK) {
        printf("navPathPlanning failed: %d\n", err);
        dominoNavModuleExitAfter();
        return 1;
    }
    printf("path_planning_result: %d\n", path_planning_result.total_tick);

    DominoRoadLine road_line = {0};
    road_line.current_location = (DominoLocationXY){.x = INT32_MIN, .y = INT32_MIN};
    road_line.region_index = 0;
    road_line.type = 0;
    road_line.start_location = (DominoLocationXY){.x = -10, .y = -10};
    road_line.target_location = (DominoLocationXY){.x = 180, .y = -90};
    dominoNavigation(&road_line);
    for (int i = 0; i < road_line.fork_road_count; i++) {
        DominoForkRoad* fork_road = dominoGetForkRoadByID(road_line.fork_road_id_list[i]);
        printf("fork_road_num: %s\n", fork_road->num);
    }

    dominoNavModuleExitAfter();
    return 0;
}
