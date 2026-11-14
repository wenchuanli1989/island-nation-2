#ifndef DOMINO_HUMAN_METRICS_H
#define DOMINO_HUMAN_METRICS_H
#include <stdint.h>
#include <stdlib.h>
/** @brief 有界单写者测量窗口；仅所有生产线程结束后查询，不参与业务决策。 */
enum { DOMINO_METRIC_SAMPLE_COUNT = 256 };
typedef struct DominoTimingSamples {
    uint64_t values[DOMINO_METRIC_SAMPLE_COUNT];
    uint64_t count;
    uint64_t total_ns;
    uint64_t max_ns;
} DominoTimingSamples;
static inline void dominoTimingRecord(DominoTimingSamples* sample, uint64_t ns) {
    sample->values[sample->count % DOMINO_METRIC_SAMPLE_COUNT] = ns;
    ++sample->count;
    sample->total_ns += ns;
    if (ns > sample->max_ns)
        sample->max_ns = ns;
}
static inline int dominoTimingCompare(const void* lhs, const void* rhs) {
    uint64_t left = *(const uint64_t*)lhs, right = *(const uint64_t*)rhs;
    return (left > right) - (left < right);
}
static inline uint64_t dominoTimingPercentile(const DominoTimingSamples* sample, uint32_t percentile) {
    size_t count = sample->count < DOMINO_METRIC_SAMPLE_COUNT ? (size_t)sample->count : DOMINO_METRIC_SAMPLE_COUNT;
    if (!count)
        return 0;
    uint64_t values[DOMINO_METRIC_SAMPLE_COUNT];
    for (size_t i = 0; i < count; ++i)
        values[i] = sample->values[i];
    qsort(values, count, sizeof(*values), dominoTimingCompare);
    return values[((count - 1) * percentile) / 100];
}
#endif
