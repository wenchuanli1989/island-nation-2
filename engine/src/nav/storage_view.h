#ifndef DOMINO_ENGINE_NAV_STORAGE_VIEW_H
#define DOMINO_ENGINE_NAV_STORAGE_VIEW_H

#include "klib/kvec.h"
#include "model_view.h"

typedef kvec_t(DominoNavDataSegment) DominoNavDataSegmentVec;
typedef kvec_t(DominoForkRoad) DominoForkRoadVec;
typedef kvec_t(DominoRoad) DominoRoadVec;
typedef kvec_t(DominoRoadLine) DominoRoadLineVec;

/** @brief Storage 恢复和写出 Nav 实体时使用的容器视图。 */
extern DominoForkRoadVec domino_all_fork_road_list;
extern DominoRoadVec domino_all_road_list;
extern DominoRoadLineVec domino_all_road_line_list;

/** @brief Storage 恢复和写出 canonical data segment 时使用的容器视图。 */
extern DominoNavDataSegmentVec domino_all_fork_road_nav_data_segment_list;
extern DominoNavDataSegmentVec domino_all_road_nav_data_segment_list;

#endif
