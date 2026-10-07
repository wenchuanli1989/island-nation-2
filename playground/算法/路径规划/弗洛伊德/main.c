#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "demo.h"
#include "domino_shared_error_codes.h"

int main(int argc, char* argv[]) {
    // 输出当前进程ID
    printf("当前进程ID: %d\n", getpid());

    // 解析命令行参数
    bool use_default = true;
    vertex_id_t vertex_num = MAX_VERTEX_COUNT;
    int performance_mode = 1;

    int opt = getopt(argc, argv, "i");
    if (opt == 'i') {
        use_default = false;
    }

    // 从控制台读取顶点数或使用默认值
    if (!use_default) {
        printf("请输入顶点数: ");
        // NOLINTNEXTLINE(cert-err34-c)
        if (scanf("%hd", &vertex_num) != 1 || vertex_num <= 0) {
            printf("错误：无效的顶点数输入\n");

            return 1;
        }
        // 清空标准输入流中剩余内容
        while (getchar() != '\n') {
        }

        // 从控制台读取性能模式，默认为性能模式
        printf("请输入性能模式 (1=性能模式, 0=调试模式): ");
        // NOLINTNEXTLINE(cert-err34-c)
        if (scanf("%d", &performance_mode) != 1 || (performance_mode != 0 && performance_mode != 1)) {
            printf("错误：无效的性能模式输入\n");

            return 1;
        }
        // 清空标准输入流中剩余内容
        while (getchar() != '\n') {
        }
    }

    printf("顶点数=%d, 性能模式=%s\n", vertex_num, !!performance_mode ? "是" : "否");

    // 分配边数据结构
    VertexEdges* vertices = calloc((size_t)vertex_num, sizeof(VertexEdges));

    // 分配路径信息矩阵
    PathInfo(*path_matrix)[vertex_num] = calloc((size_t)vertex_num * (size_t)vertex_num, sizeof(PathInfo));

    int zero_count = 0;
    // 初始化所有边权重（每个点最多连接8个点）
    for (int i = 0; i < vertex_num; i++) {
        int count = 0;
        for (int j = 0; j < vertex_num && count < MAX_EDGES_PER_VERTEX; j++) {
            if (i == j) {
                continue;
            }

            // unsigned int random_1 = 0;
            // getrandom(&random_1, sizeof(random_1), 0);

            bool edge_conflict = false;
            for (int k = 0; k < vertices[j].count; k++) {
                if (vertices[j].edges[k].target_vertex_id == i) {
                    edge_conflict = true;
                    break;
                }
            }

            if (!edge_conflict && vertices[j].count < MAX_EDGES_PER_VERTEX) {
                unsigned int random_2 = 0;
                getrandom(&random_2, sizeof(random_2), 0);
                vertices[i].edges[count].weight = (edge_weight_t)(200 + (int)(random_2 % 200));
                vertices[i].edges[count].target_vertex_id = (vertex_id_t)j;
                vertices[i].count++;

                vertices[j].edges[vertices[j].count].weight = (edge_weight_t)(200 + (int)(random_2 % 200));
                vertices[j].edges[vertices[j].count].target_vertex_id = (vertex_id_t)i;
                vertices[j].count++;
                count++;
            }
        }

        if (vertices[i].count == 0) {
            zero_count++;
        }
    }
    printf("零度节点数量: %d\n", zero_count);
    printf("零度节点比例: %f\n", (float)zero_count / (float)vertex_num);

    // 设置测试边0->1和0->900
    vertices[0].count = 2;
    vertices[0].road_fork_id = 900000;
    vertices[0].edges[0].weight = 1;
    vertices[0].edges[0].target_vertex_id = 1;
    vertices[0].edges[1].weight = 1;
    vertices[0].edges[1].target_vertex_id = 900;

    // 设置测试边900->5
    vertices[900].count = 1;
    vertices[900].road_fork_id = 900900;
    vertices[900].edges[0].weight = 1;
    vertices[900].edges[0].target_vertex_id = 5;

    // 设置测试边1->2和1->5
    vertices[1].count = 2;
    vertices[1].road_fork_id = 900001;
    vertices[1].edges[0].weight = 2;
    vertices[1].edges[0].target_vertex_id = 2;
    vertices[1].edges[1].weight = 20;
    vertices[1].edges[1].target_vertex_id = 5;

    // 设置测试边2->3和2->5
    vertices[2].count = 2;
    vertices[2].road_fork_id = 900002;
    vertices[2].edges[0].weight = 3;
    vertices[2].edges[0].target_vertex_id = 3;
    vertices[2].edges[1].weight = 100;
    vertices[2].edges[1].target_vertex_id = 5;

    // 设置测试边3->4
    vertices[3].count = 2;
    vertices[3].road_fork_id = 900003;
    vertices[3].edges[0].weight = 5;
    vertices[3].edges[0].target_vertex_id = 4;
    vertices[3].edges[1].weight = 5;
    vertices[3].edges[1].target_vertex_id = 5;

    // 设置测试边4->5
    vertices[4].count = 1;
    vertices[4].road_fork_id = 900004;
    vertices[4].edges[0].weight = 6;
    vertices[4].edges[0].target_vertex_id = 5;

    vertices[5].count = 0;
    vertices[5].road_fork_id = 900005;

    printf("PathInfo size: %zu\n", sizeof(PathInfo));
    printf("内存占用: %zu KB\n\n", ((sizeof(PathInfo) * (size_t)vertex_num * (size_t)vertex_num)) / 1024);
    printf("\n========================================\n");
    printf("开始弗洛伊德算法: \n");
    clock_t start = clock();
    FloydPathPlanningResult floyd_path_planning_result = floydPathPlanning(vertex_num, vertices, path_matrix);

    clock_t end = clock();
    double cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("弗洛伊德算法执行时间: %f seconds\n", cpu_time_used);

    printf("算法初始化时间: %f seconds\n", floyd_path_planning_result.init_time_used);
    printf("核心算法时间: %f seconds\n", floyd_path_planning_result.core_time_used);
    printf("内层循环次数: %ld\n", floyd_path_planning_result.total_tick);
    printf("内层循环命中次数: %ld\n", floyd_path_planning_result.hit_tick);
    printf("内层循环空权重次数: %ld\n", floyd_path_planning_result.null_weight_count);
    printf("内层循环空权重次数(内部): %ld\n", floyd_path_planning_result.null_weight_count_inner);

    if (!performance_mode) {
        // 输出path矩阵
        printf("path矩阵: \n");
        for (int i = 0; i < vertex_num; i++) {
            for (int j = 0; j < vertex_num; j++) {
                printf("%5d", path_matrix[i][j].pre_vertex_id);
            }
            printf("\n\n");
        }

        // 输出最终边连接信息（保持不变，因为算法不修改原始边结构）
        printf("最终边连接信息: \n");
        for (int i = 0; i < vertex_num; i++) {
            printf("顶点 %d 连接到: ", i);
            const VertexEdges* vertex = &vertices[i];
            for (int j = 0; j < vertex->count; j++) {
                printf("(%d, 权重=%d) ", vertex->edges[j].target_vertex_id, vertex->edges[j].weight);
            }
            printf("\n");
        }
        printf("\n");
    }

    vertex_id_t start_vertex_id = 0;
    vertex_id_t end_vertex_id = 5;
    printf("\n========================================\n");
    printf("节点%d到节点%d\n", start_vertex_id, end_vertex_id);
    // 输出节点0到节点5的最短路径权重
    if (path_matrix[start_vertex_id][end_vertex_id].shortest_distance == WEIGHT_INFINITY) {
        printf("无最短路径\n");
    } else {
        // 输出节点0到节点5的最短路径信息
        printf("最短路径信息：\n");
        enum { ROAD_FORK_MAX_COUNT = MAX_VERTEX_COUNT };
        PathInfo shortest_path_info[ROAD_FORK_MAX_COUNT] = {0};
        int road_fork_count = 0;

        printf("%d(%d) -> ", start_vertex_id, (int)vertices[start_vertex_id].road_fork_id);
        DOMINO_CODE result =
            findShortestPath(shortest_path_info, ROAD_FORK_MAX_COUNT, &road_fork_count, vertex_num, path_matrix, start_vertex_id, end_vertex_id);
        assertDominoErrorCode(result, true, __FILE__, __LINE__);
        for (int i = road_fork_count - 1; i >= 0; i--) {
            printf("%d(%d) -> ", shortest_path_info[i].pre_vertex_id, (int)shortest_path_info[i].pre_road_fork_id);
        }
        printf("%d(%d)\n", end_vertex_id, (int)vertices[end_vertex_id].road_fork_id);

        printf("最短路径权重: %d\n", path_matrix[start_vertex_id][end_vertex_id].shortest_distance);
    }

    // 释放内存
    free(vertices);
    free(path_matrix);

    return 0;
}
