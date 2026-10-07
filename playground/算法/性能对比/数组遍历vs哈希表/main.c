/*
 * klib 哈希表读取速度对比：值类型 64 字节 vs 4000 字节
 * 两个哈希表均为：key = uint64_t（8 字节无符号整数），各存储 10000 条数据
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "khash.h"

/* 64 字节值类型 */
typedef struct {
    unsigned char d[64];
} Val64;
/* 4000 字节值类型 */
typedef struct {
    unsigned char d[4000];
} Val4000;

KHASH_MAP_INIT_INT64(u64_v64, Val64)
KHASH_MAP_INIT_INT64(u64_v4000, Val4000)

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

#define N 10000
#define LOOKUPS 500000

static void fill_u64_v64(khash_t(u64_v64) * h, uint64_t* keys) {
    for (size_t i = 0; i < N; i++) {
        uint64_t k = (uint64_t)rand() << 32 | (uint64_t)rand();
        keys[i] = k;
        int ret;
        khiter_t it = kh_put(u64_v64, h, (khint64_t)k, &ret);
        memset(&kh_value(h, it), (int)(k & 0xff), sizeof(Val64));
    }
}

static void fill_u64_v4000(khash_t(u64_v4000) * h, uint64_t* keys) {
    for (size_t i = 0; i < N; i++) {
        uint64_t k = (uint64_t)rand() << 32 | (uint64_t)rand();
        keys[i] = k;
        int ret;
        khiter_t it = kh_put(u64_v4000, h, (khint64_t)k, &ret);
        memset(&kh_value(h, it), (int)(k & 0xff), sizeof(Val4000));
    }
}

static void pick_test_keys(const uint64_t* keys, uint64_t* test_keys, size_t m) {
    for (size_t i = 0; i < m; i++)
        test_keys[i] = keys[rand() % N];
}

/* 读取并触碰整块 value，避免被优化掉 */
static double bench_u64_v64(khash_t(u64_v64) * h, const uint64_t* test_keys, size_t lookups) {
    volatile unsigned long sum = 0;
    double t0 = now();
    for (size_t i = 0; i < lookups; i++) {
        khiter_t it = kh_get(u64_v64, h, (khint64_t)test_keys[i]);
        if (it != kh_end(h)) {
            const Val64* v = &kh_value(h, it);
            for (size_t j = 0; j < sizeof(Val64); j++)
                sum += v->d[j];
        }
    }
    double t1 = now();
    (void)sum;
    return t1 - t0;
}

static double bench_u64_v4000(khash_t(u64_v4000) * h, const uint64_t* test_keys, size_t lookups) {
    volatile unsigned long sum = 0;
    double t0 = now();
    for (size_t i = 0; i < lookups; i++) {
        khiter_t it = kh_get(u64_v4000, h, (khint64_t)test_keys[i]);
        if (it != kh_end(h)) {
            const Val4000* v = &kh_value(h, it);
            for (size_t j = 0; j < sizeof(Val4000); j++)
                sum += v->d[j];
        }
    }
    double t1 = now();
    (void)sum;
    return t1 - t0;
}

int main(void) {
    printf("klib 哈希表读取速度：值 64 字节 vs 4000 字节（key=uint64_t，各 %d 条）\n", N);
    printf("每次测试 %d 次随机查找（每次查找会读取整块 value）\n\n", LOOKUPS);

    srand((unsigned)time(nullptr));

    uint64_t* keys = malloc(N * sizeof(uint64_t));
    uint64_t* test_keys = malloc(LOOKUPS * sizeof(uint64_t));
    if (!keys || !test_keys) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    /* 值类型 64 字节 */
    khash_t(u64_v64)* h64 = kh_init(u64_v64);
    fill_u64_v64(h64, keys);
    pick_test_keys(keys, test_keys, LOOKUPS);
    double t64 = bench_u64_v64(h64, test_keys, LOOKUPS);

    /* 值类型 4000 字节 */
    khash_t(u64_v4000)* h4000 = kh_init(u64_v4000);
    fill_u64_v4000(h4000, keys);
    pick_test_keys(keys, test_keys, LOOKUPS);
    double t4000 = bench_u64_v4000(h4000, test_keys, LOOKUPS);

    printf("%10s %10s %12s %14s\n", "值大小", "条数", "耗时(秒)", "万次/秒");
    printf("---------- ---------- ------------ --------------\n");
    printf("%10s %10d %12.6f %14.2f\n", "64 B", N, t64, LOOKUPS / (t64 * 1e4));
    printf("%10s %10d %12.6f %14.2f\n", "4000 B", N, t4000, LOOKUPS / (t4000 * 1e4));

    kh_destroy(u64_v64, h64);
    kh_destroy(u64_v4000, h4000);
    free(keys);
    free(test_keys);

    printf("\n结论：值越大，每次读取触及的 cache 行越多，整体读取越慢。\n");
    return 0;
}
