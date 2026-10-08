# Domino游戏服务器项目编码规范

本文档参考了业界优秀开源项目（Redis、Nginx、Linux内核等）的最佳实践，为项目提供统一的编码规范。

## 目录

1. [代码风格规范](#代码风格规范)
2. [命名规范](#命名规范)
3. [错误处理规范](#错误处理规范)
4. [内存管理规范](#内存管理规范)
5. [线程安全规范](#线程安全规范)
6. [网络编程规范](#网络编程规范)
7. [日志规范](#日志规范)
8. [文档规范](#文档规范)
9. [性能优化规范](#性能优化规范)
10. [安全规范](#安全规范)
11. [时间语义规范](#时间语义规范)

---

## 代码风格规范


### 1.4 指针声明

- **指针符号靠近类型名**（`int* ptr`），与`.clang-format`配置一致
- 多个指针声明时，每个变量单独声明

```c
// ✅ 正确
int* ptr1;
int* ptr2;

// ❌ 错误：容易混淆
int* ptr1, ptr2;  // ptr2不是指针！
```


---

## 命名规范

### 2.1 文件命名

- 使用**小写字母**和**下划线**
- 文件名应清晰表达文件功能

```
✅ 正确：
- game_server.c
- player_manager.h
- network_utils.c

❌ 错误：
- DominoServer.c
- playerManager.h
- network-utils.c
```

### 2.2 函数命名

- 使用小驼峰命名，动词开头
- 函数名应清晰表达功能

```c
// ✅ 正确
int createPlayer(const char* name);
void destroyPlayer(Player* player);
ssize_t sendMessage(int fd, const void* buf, size_t len);

// ❌ 错误
int CreatePlayer(const char* name);  // 大驼峰
void destroy(Player* p);            // 不清晰
```

### 2.3 变量命名

- 使用**小写字母**和**下划线**
- 局部变量使用简短名称，全局变量使用描述性名称
- 循环变量可以使用`i`, `j`, `k`等
- _ptr -> 指针
- _str -> 字符串
- 数字类型后缀带有含义 _count，_size，_len，_num，_index，_max，_min，_default，_flag
- _buf -> 缓冲区
- 时间量须用 `_ns`、`_ms`、`_s` 等后缀明确单位，例如 `due_ms`、`delay_ms`、`runtime_ns`；同名单位必须遵循下文的现实计时语义。

```c
// ✅ 正确
int player_count;
int max_clients = 100;
for (int i = 0; i < count; i++) {
    process_item(items[i]);
}

// ❌ 错误
int PlayerCount;      // 大驼峰
int maxClients;       // 驼峰
int idx;              // 不清晰（除非上下文明确）
```

### 2.4 常量命名

- 宏，枚举值 使用**全大写字母**和**下划线**

```c
// ✅ 正确
#define MAX_PLAYERS 1000
#define BUFFER_SIZE 4096
#define DEFAULT_PORT 8080

typedef enum {
    PLAYER_STATUS_ONLINE = 1,
    PLAYER_STATUS_OFFLINE = 2,
    PLAYER_STATUS_AWAY = 3
} Player_Status;

// ❌ 错误
#define maxPlayers 1000
#define buffer_size 4096
```

### 2.5 结构体，联合体命名

- 使用大驼峰命名

```c

typedef struct {
    int id;
} DominoPlayer;  
```

### 2.6 静态变量和私有函数

- 使用`static`关键字标记模块内部函数和变量

```c
// ✅ 正确
static int internal_counter = 0;

static void process_internal_data(void) {
    // 内部实现
}

```

### 基本类型命名
```c
typedef uint32_t object_id_t;

```

---

## 错误处理规范

### 3.1 返回值约定

- **成功返回0或正值**，**失败返回负值或特定错误码**
- 使用统一的错误码定义

```c
// ✅ 正确：定义错误码
typedef enum {
    CODE_OK = 0,
    ERR_INVALID_PARAM = -1,
    ERR_MEMORY_ALLOC = -2,
    ERR_NETWORK = -3,
    ERR_NOT_FOUND = -4
} ERROR_CODE;

int create_player(const char* name, Player** out_player) {
    if (!name || !out_player) {
        return ERR_INVALID_PARAM;
    }
    
    Player* player = malloc(sizeof(Player));
    if (!player) {
        return ERR_MEMORY_ALLOC;
    }
    
    *out_player = player;
    return CODE_OK;
}
```

### 3.2 错误检查

- **所有可能失败的函数调用都要检查返回值**
- 使用`goto`进行资源清理（Linux内核风格）

```c
// ✅ 正确：使用goto清理资源
int setup_server(int port) {
    int server_fd = -1;
    int client_fd = -1;
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        goto error;
    }
    
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        goto error;
    }
    
    return CODE_OK;

error:
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    return ERR_NETWORK;
}
```

### 3.3 断言使用

- 使用`assert()`检查**不应该发生**的情况（开发阶段）
- **不要**用断言检查用户输入或外部数据
- 生产环境可以禁用断言（`NDEBUG`）

```c
#include <assert.h>

// ✅ 正确：检查内部逻辑
void process_data(int* data, size_t count) {
    assert(data != NULL);  // 内部调用，不应该为NULL
    assert(count > 0);     // 内部逻辑，不应该为0
    
    // 处理数据
}

// ❌ 错误：用断言检查用户输入
int validate_user_input(const char* input) {
    assert(input != NULL);  // 错误！用户输入可能为NULL
    // 应该使用if检查
}
```

---

## 内存管理规范


### 4.4 缓冲区安全

- 使用`strncpy`代替`strcpy`，并确保字符串以`\0`结尾
- 检查缓冲区边界，防止溢出

```c
// ✅ 正确
char buffer[64];
strncpy(buffer, source, sizeof(buffer) - 1);
buffer[sizeof(buffer) - 1] = '\0';

// ❌ 错误
char buffer[64];
strcpy(buffer, source);  // 可能溢出
```

---

## 线程安全规范

多线程统一使用 C23 标准 API：线程、互斥锁、条件变量和线程局部存储使用 `<threads.h>` 的 `thrd_*`、`mtx_*`、`cnd_*`、`tss_*`，一次性初始化使用 `call_once`，原子操作使用 `<stdatomic.h>`。项目自有代码不直接使用 pthread 或操作系统线程 API，也不将 `mtx_t`、`cnd_t` 等标准同步对象强转为原生类型。第三方库和标准库底层实现不在此限制范围内。

### 5.1 线程安全函数

- 明确标记函数是否线程安全
- 使用互斥锁保护共享数据
- 避免死锁（按固定顺序获取锁）
- 在工作线程启动前完成同步对象初始化；仅在初始化成功且所有使用者退出后销毁

```c
#include <stdint.h>
#include <threads.h>

// ✅ 正确：线程安全的计数器
static uint64_t counter_count = 0;
static mtx_t counter_mutex;

/** @brief 控制线程在启动工作线程前调用一次；失败时不能使用或销毁该锁。 */
int initCounter(void) {
    return mtx_init(&counter_mutex, mtx_plain) == thrd_success ? 0 : -1;
}

/** @brief 读取计数器；成功返回 0，失败返回 -1。 */
int getCounter(uint64_t* count_ptr) {
    if (count_ptr == nullptr || mtx_lock(&counter_mutex) != thrd_success) {
        return -1;
    }
    *count_ptr = counter_count;
    return mtx_unlock(&counter_mutex) == thrd_success ? 0 : -1;
}

/** @brief 递增计数器；成功返回 0，锁操作失败或计数耗尽时返回 -1。 */
int incrementCounter(void) {
    if (mtx_lock(&counter_mutex) != thrd_success) {
        return -1;
    }
    int result = -1;
    if (counter_count < UINT64_MAX) {
        counter_count++;
        result = 0;
    }
    if (mtx_unlock(&counter_mutex) != thrd_success) {
        return -1;
    }
    return result;
}

/** @brief 控制线程确认所有使用者已退出后调用；仅用于成功初始化的锁。 */
void destroyCounter(void) {
    mtx_destroy(&counter_mutex);
}
```

### 5.2 原子操作

- 简单操作使用 C23 标准原子操作（`<stdatomic.h>`）

```c
#include <stdatomic.h>

// ✅ 正确：使用原子操作
static atomic_int player_count = 0;

void addPlayer(void) {
    atomic_fetch_add(&player_count, 1);
}

int getPlayerCount(void) {
    return atomic_load(&player_count);
}
```




## 文档规范

### 8.1 文件头注释

- 每个源文件包含文件头注释
- 说明文件用途、作者、许可证

```c
/**
 * @file server.c
 * @brief 游戏服务器主程序
 * @author Your Name
 * @date 2024-01-01
 * 
 * 实现TCP游戏服务器的核心功能，包括：
 * - 客户端连接管理
 * - 消息处理
 * - 游戏逻辑协调
 */
```

### 8.2 函数注释

- 使用Doxygen风格注释
- 说明参数、返回值、副作用

```c
/**
 * @brief 创建新玩家对象
 * @param name 玩家名称（不能为空，长度1-63字符）
 * @param x, y, z 初始位置坐标
 * @return 成功返回玩家指针，失败返回NULL
 * @note 调用者负责使用destroy_player()释放内存
 */
Player* create_player(const char* name, float x, float y, float z);
```

### 8.3 复杂逻辑注释

- 解释**为什么**这样做，而非**做什么**
- 复杂算法添加注释

```c
// ✅ 正确：解释为什么
// 使用平方距离避免开方运算，提高性能
float dist_sq = dx * dx + dy * dy + dz * dz;
if (dist_sq < radius_sq) {
    // 在范围内
}

// ❌ 错误：只说明做什么
// 计算距离
float dist = sqrt(dx * dx + dy * dy + dz * dz);
```

---

## 性能优化规范

### 9.1 避免不必要的计算

- 缓存计算结果
- 避免在循环中进行重复计算

```c
// ✅ 正确：缓存计算结果
float radius_sq = radius * radius;  // 计算一次
for (int i = 0; i < count; i++) {
    float dist_sq = calculate_distance_sq(players[i].x, players[i].y, 
                                          center_x, center_y);
    if (dist_sq < radius_sq) {
        // 处理
    }
}

// ❌ 错误：重复计算
for (int i = 0; i < count; i++) {
    float dist = sqrt(calculate_distance_sq(...));  // 每次开方
    if (dist < radius) {
        // 处理
    }
}
```

### 9.2 内存对齐

- 结构体成员按大小排序（减少padding）

```c
// ✅ 正确：优化内存布局
typedef struct {
    uint64_t id;        // 8字节
    uint32_t count;     // 4字节
    uint16_t flags;     // 2字节
    uint8_t status;     // 1字节
    // padding: 1字节
} 

// ❌ 错误：未优化
typedef struct {
    uint8_t status;     // 1字节 + 7字节padding
    uint64_t id;        // 8字节
    uint16_t flags;     // 2字节 + 6字节padding
    uint32_t count;     // 4字节
} UnoptimizedStruct;  // 24字节（浪费8字节）
```

### 9.3 内联函数

- 小函数使用`inline`关键字
- 在头文件中定义

```c
// ✅ 正确：内联小函数
static inline int clamp(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}
```

### 9.4 编译器优化

- 使用适当的编译优化选项
- 关键路径使用`__attribute__((hot))`

```c
// ✅ 正确：标记热点函数
__attribute__((hot))
void process_game_tick(void) {
    // 频繁调用的函数
}
```

---

## 安全规范

### 10.1 输入验证

- **所有外部输入都要验证**
- 检查长度、范围、格式

```c
// ✅ 正确：验证输入
int validate_player_name(const char* name) {
    if (!name) {
        return 0;
    }
    
    size_t len = strlen(name);
    if (len == 0 || len >= 64) {
        return 0;
    }
    
    // 检查字符集（只允许字母数字下划线）
    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') {
            return 0;
        }
    }
    
    return 1;
}
```

### 10.2 整数溢出

- 检查整数运算溢出
- 使用`-ftrapv`编译选项（开发阶段）

```c
// ✅ 正确：检查溢出
int safe_add(int a, int b, int* result) {
    if ((b > 0 && a > INT_MAX - b) || 
        (b < 0 && a < INT_MIN - b)) {
        return -1;  // 溢出
    }
    *result = a + b;
    return 0;
}
```

### 10.3 格式化字符串

- 使用固定格式字符串，不要使用用户输入作为格式

```c
// ✅ 正确
printf("玩家: %s\n", player_name);

// ❌ 错误
printf(player_name);  // 危险！如果player_name包含%格式符
```

### 10.4 敏感数据

- 密码、密钥等敏感数据使用后立即清零
- 不要硬编码密钥

```c
// ✅ 正确：清零敏感数据
void clear_password(char* password, size_t len) {
    if (password) {
        memset(password, 0, len);
    }
}
```

---

## 时间语义规范

- 业务时刻和时长统一使用 `domino_runtime_ms_t`（`uint32_t`），表示 32 位累计未冻结现实毫秒。`dominoTimeGetRuntimeMs(&now_ms)` 返回状态码，成功时输出 `runtime_ns / 1000000`，换算结果超出 `UINT32_MAX` 毫秒时返回 `ERR_OUT_OF_RANGE` 且不改写输出；不应用游戏日历倍率。冻结期间累计基准不增长，存档保存该基准，加载后继续累计。
- `0` 与 `UINT32_MAX` 均合法；相加前检查 `delta_ms <= UINT32_MAX - base_ms`，相减前确认先后关系，禁止回绕、截断、饱和或归零。约 49.71 个未冻结现实日（按当前日历约 248.55 个游戏年）的累计范围已被接受，不得据此或跨存档累计自行改用、建议改用 64 位业务毫秒。
- time 内部采样和存档基准使用 64 位纳秒保留精度；64 位 UTC 墙上时间戳用于日志及文件命名，消息／计时器 ID 也可为 64 位。这些类型不能当作业务毫秒类型的依据；任务、timer 和收益结算须使用经过范围检查的 32 位毫秒入口。
- timer 延迟与到期时刻、任务持续时间、NPC AI 的处理节奏、阈值时间和收益结算全部使用现实时间单位。游戏日期及日内时分只由显示层按倍率换算，不能作为另一条业务计时轴。
- 时间变量、参数、结构体字段和指标名须明确 `_ns`、`_ms`、`_s` 等单位，换算时只转换现实时间的单位，不把游戏日历倍率混入业务计算。
- 单次游戏会话最多持续 8 个现实小时，届时保存退出、再次加载存档继续，是产品要求，当前未实现自动保存退出。单次会话时长单独统计；跨存档累计的未冻结现实时间继续增长，不能通过重置累计基准实现会话上限。

---

## 其他最佳实践



### 11.2 包含顺序

- 系统头文件 → 第三方库 → 项目头文件
- 使用`clang-format`自动排序（已配置）

```c
// ✅ 正确
#include <stdio.h>      // 系统头文件
#include <stdlib.h>
#include <string.h>

#include "game_server.h"  // 项目头文件
#include "player.h"
```

### 11.3 未使用变量

- 使用`(void)variable`标记有意未使用的变量
- 或使用`__attribute__((unused))`

```c
// ✅ 正确
static void signal_handler(int sig) {
    (void)sig;  // 标记未使用
    running = 0;
}

// 或
static void signal_handler(int sig __attribute__((unused))) {
    running = 0;
}
```


---

## 工具和检查


### 12.3 动态分析

```bash
# 使用Valgrind检查内存泄漏
valgrind --leak-check=full --show-leak-kinds=all ./game_server

# 使用AddressSanitizer
gcc -fsanitize=address -g -o game_server server.c
```


## 网络通信

### 网络数据包大小
尽量控制在MTU大小范围内
