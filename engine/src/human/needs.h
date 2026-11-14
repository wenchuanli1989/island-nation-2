#ifndef DOMINO_HUMAN_NEEDS_H
#define DOMINO_HUMAN_NEEDS_H
#include "domino_shared_human.h"
/** @brief 自然需求的必要局部值，结算与预测使用同一基础计算。 */
typedef struct DominoNeedValues {
    domino_runtime_ms_t phase_ms;
    uint8_t energy, food, water, hygiene, recreation, social;
} DominoNeedValues;
DominoNeedValues dominoHumanReadNeeds(const DominoHumanData* data);
void dominoHumanWriteNeeds(DominoHumanData* data, DominoNeedValues values);
DominoNeedValues dominoHumanCalculateNeeds(DominoNeedValues values, domino_runtime_ms_t begin_ms, domino_runtime_ms_t end_ms);
#endif
