#ifndef DEMO_H
#define DEMO_H

#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

typedef uint16_t vertex_id_t;    // 点ID类型
typedef uint32_t edge_weight_t;  // 边权值类型，考虑最短路径上节点权重总和

enum {
    WEIGHT_INFINITY = UINT8_MAX,   // 边权值无穷大
    PRE_VERTEX_NULL = UINT16_MAX,  // 路径前驱顶点为空
    MAX_EDGES_PER_VERTEX = 32,     // 每个点最多连接的边数
    MAX_VERTEX_COUNT = 3000        // 顶点数最大值
};

/**
 * @brief 边信息结构体，包含权重和目标顶点
 */
typedef struct {
    edge_weight_t weight;          // 边权重
    vertex_id_t target_vertex_id;  // 目标顶点
} Edge;

/**
 * @brief 顶点边信息结构体，包含该顶点的所有边和边数量
 */
typedef struct {
    Edge edges[MAX_EDGES_PER_VERTEX];  // 该顶点的边数组
    domino_address_id_t road_fork_id;
    int count;  // 该顶点的边数量
} VertexEdges;

/**
 * @brief 路径信息结构体，包含路径前驱和权重
 */
typedef struct {
    domino_address_id_t pre_road_fork_id;
    float pre_road_fork_x;            // 前驱分叉X坐标
    float pre_road_fork_y;            // 前驱分叉Y坐标
    edge_weight_t shortest_distance;  // 最短距离
    vertex_id_t pre_vertex_id;        // 路径前驱顶点

    uint32_t total_hit_count;
    uint32_t current_start_time;  // 开始时间
    uint16_t current_hit_count;   // 当前命中计数

    vertex_id_t middle_vertex_count;  // 中间顶点计数
    uint16_t state_info;              // 状态信息
    uint8_t hot_upgrade_weight;       // 热点升级权重
    uint8_t weight_scale;             // 权重缩放
} PathInfo;

/**
 * @brief 查找最短路径
 *
 * @param shortest_path_info 最短路径信息
 * @param road_fork_max_count 路径分叉ID数组最大长度
 * @param road_fork_count 路径分叉ID数量
 * @param vertex_num 顶点数
 * @param path_matrix 路径信息矩阵
 * @param start_vertex_id 起始顶点
 * @param end_vertex_id 终点顶点
 */
DOMINO_CODE findShortestPath(PathInfo* shortest_path_info, int road_fork_max_count, int* road_fork_count, vertex_id_t vertex_num,
                             PathInfo (*path_matrix)[vertex_num], vertex_id_t start_vertex_id, vertex_id_t end_vertex_id);

typedef struct {
    double init_time_used;
    double core_time_used;
    long total_tick;
    long hit_tick;
    long null_weight_count;
    long null_weight_count_inner;
} FloydPathPlanningResult;

/**
 * @brief 弗洛伊德算法函数
 *
 * @param vertex_num 顶点数
 * @param vertices 每个顶点的边信息数组
 * @param path_matrix 路径信息矩阵
 */
FloydPathPlanningResult floydPathPlanning(vertex_id_t vertex_num, const VertexEdges* vertices, PathInfo (*path_matrix)[vertex_num]);
#endif
