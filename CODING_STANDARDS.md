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

- 业务与控制接口成功返回 0 或正值，拒绝请求、查询未命中、队列为空或正常排空结束等仍返回约定结果；使用项目的 `DOMINO_CODE`，不按负结果码统一终止进程。
- 致命错误在初始化、开始、停止、冻结、解冻、销毁和运行阶段都通过 `DOMINO_EXIT_WITH_ERROR` 直接输出 stderr，并由 `common.c` 中的公共方法执行 `exit(EXIT_FAILURE)`，允许标准退出处理。致命出口不进入日志队列、不等待日志排空或 join。
- 没有可恢复业务结果的生命周期接口返回 `void`；不保存实例首错或跨线程传播致命错误，保留正常启动就绪、命令完成、退出通知、join 和资源所有权。
- 公共通用工具保留头文件约定的输入校验、输出状态和返回结果，不因当前调用方已校验而删除独立调用边界。内部辅助函数复用调用方已确认且仍有效的条件，不重复检查、清零或维护可直接推导的计数；成功调用会完整写入的局部输出无需预清零。实际 IO、分配和格式化失败仍须检查，文件状态在磁盘操作后按需重新读取。
- 项目自有模块的生命周期方法统一使用 Init／Start／Stop／Destroy，不保留旧名或兼容别名；只提供实际需要的阶段，无独立线程的子模块无需空的 Start／Stop。
- 引擎对外生命周期入口串行且严格校验顺序及调用线程：Init 仅接受未初始化，Start 仅接受已初始化，Stop 接受已初始化或运行中，Destroy 仅接受已停止。重复调用、未初始化操作、未 Stop 就 Destroy、Stop 后直接 Start 均通过 `DOMINO_EXIT_WITH_ERROR` 输出 stderr 并退出。受引擎编排的内部模块和辅助调用链遵循资源有效的契约，不重复检查已由引擎保证的模块、索引或固定队列初始化条件，不维护仅为这些重复校验存在的状态或查询接口；无线程模块只需 Init／Destroy。可直接调用的公共业务入口仍负责访问资格及输入检查。业务参数、实体与任务事实、版本、容量及时间范围校验，以及线程就绪、业务完成、队列排空和真实资源失败检查仍保留。shared logger／queue 是独立公共接口，继续保留各自的生命周期边界校验；引擎日志包装层不额外维护状态或重复检查。 shared 的公共调用路径集中校验参数与实例状态，内部辅助函数依赖已建立的前置条件，不重复检查；条件等待会释放锁，唤醒后的排空状态、容量和唤醒版本等动态条件仍须在锁内重新检查。
- 线程模块统一拆分 Init／Start／Stop／Destroy：Init 准备固定资源，不创建线程；静态模块的首次空状态由静态初值建立，后续由 Destroy 复位，不在 Init 重复清空；通用 queue／logger 首次对象须零初始化，Init 先校验生命周期再建立内部资源；Start 只使用已有资源启动线程；引擎 Stop 仅在运行过时调用 human／timer Stop 并 join、结算；仅 Init 时跳过这些调用，两条路径均调用 shared logger Stop 关闭入队并刷新；Destroy 负责保存、释放资源并立即复位实例状态：容器指针／数量／容量、索引指针和状态字段恢复为空或初始值，不把旧状态留给下一次 Init 清理。time 的内存快照只能在存档完成后清空，不能改变文件中的累计时间。引擎先停止全部业务写入再统一结算，最后停止 logger。业务数据按需扩容及序列化临时分配不属于固定运行资源。
- 运行阶段的非致命错误及正常日志走日志模块，按模块文件、实例默认文件、stdout 的顺序选择已配置的输出目标；初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断统一使用 `DOMINO_PRINT_MESSAGE` 直接输出 stdout。 阶段按操作所属路径区分：停止操作自身直接输出，业务线程最后一批操作仍使用运行日志；先 join 全部业务线程，再排空并停止 logger，最后销毁资源。由掌握业务语义的边界记录一次，通用工具保留结果，不逐层重复打印；错误日志级别本身不决定是否退出。

```c
#include <stdlib.h>

#include "domino_shared_common.h"

// ✅ 正确：无效业务请求保留结果，实际分配失败直接退出。
DOMINO_CODE createPlayer(const char* name_str, Player** out_player_ptr) {
    if (name_str == nullptr || out_player_ptr == nullptr) {
        return ERR_INVALID_PARAM;
    }

    Player* player_ptr = malloc(sizeof(*player_ptr));
    if (player_ptr == nullptr) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate player");
    }

    *out_player_ptr = player_ptr;
    return CODE_OK;
}
```

### 3.2 错误检查

- 检查有业务或控制结果的函数返回值，并按调用场景判断拒绝请求、继续正常流程或直接致命退出；仅成功或退出的 `void` 接口不再包裹错误传播。
- 正常销毁和可恢复操作使用 `goto` 集中清理已取得的资源。致命错误直接退出，不额外建立失败清理、回滚或跨线程通知链。
- 以下示例读取可选输入文件，普通 IO 失败可由调用方处理；存档加载或保存等不能继续运行的场景应在其业务边界转为致命错误。

```c
#include <stdio.h>

#include "domino_shared_error_codes.h"

// ✅ 正确：可恢复读取失败保留具体原因，并关闭本次拥有的文件。
DOMINO_CODE readOptionalFile(const char* path_str, void* buffer_ptr, size_t buffer_size) {
    if (path_str == nullptr || buffer_ptr == nullptr || buffer_size == 0) {
        return ERR_INVALID_PARAM;
    }
    FILE* file_ptr = fopen(path_str, "rb");
    if (file_ptr == nullptr) {
        return ERR_FILE_OPEN;
    }
    DOMINO_CODE result = CODE_OK;
    if (fread(buffer_ptr, 1, buffer_size, file_ptr) != buffer_size) {
        result = ERR_FILE_READ;
        goto cleanup;
    }

cleanup:
    if (fclose(file_ptr) != 0 && result == CODE_OK) {
        result = ERR_FILE_CLOSE;
    }
    return result;
}
```

### 3.3 断言使用

- 使用`assert()`检查**不应该发生**的情况（开发阶段）
- **不要**用断言检查用户输入或外部数据
- 生产环境可以禁用断言（`NDEBUG`）；正式运行必须处理的致命错误要显式检查并调用公共退出方法，不能仅依赖断言。

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

### 4.1 内存分配失败策略

- **实际内存分配失败或分配大小溢出直接打印 stderr 并 exit。** 保留必要的空指针、返回值和乘法溢出检查，不通过多层错误返回或实例首错状态传播，不新增重试、降级、提前占位或复杂回滚。
- 优先使用 `kv_resize`、`kv_push` 等公开容器接口维护存储。`lib/` 下第三方库代码保持原样，包括 `kvec` 原有分配失败终止行为和第三方断言；项目自有分配检查使用公共退出方法，不通过操作容器内部字段绕开公开接口。
- 业务容量、参数、格式、IO 错误和零长度请求根据各自语义保留可恢复结果；不能把实际分配失败混同于容量拒绝，也不能用未检查空指针导致的未定义行为代替错误处理。

```c
#include <stdlib.h>

#include "domino_shared_common.h"

// ✅ 正确：在实际分配点检查结果，直接报告致命错误。
Player* player_ptr = malloc(sizeof(*player_ptr));
if (player_ptr == nullptr) {
    DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate player");
}
```


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

#include "domino_shared_common.h"

// ✅ 正确：同步故障直接退出，业务计数容量仍可返回给调用方。
static uint64_t counter_count = 0;
static mtx_t counter_mutex;

/** @brief 控制线程在启动工作线程前调用一次；初始化失败直接退出。 */
void initCounter(void) {
    if (mtx_init(&counter_mutex, mtx_plain) != thrd_success) {
        DOMINO_EXIT_WITH_ERROR(ERR_MUTEX_INIT, "failed to initialize counter mutex");
    }
}

/** @brief 读取计数器；空输出指针返回业务校验结果，同步故障直接退出。 */
DOMINO_CODE getCounter(uint64_t* count_ptr) {
    if (count_ptr == nullptr) {
        return ERR_NULL_POINTER;
    }
    if (mtx_lock(&counter_mutex) != thrd_success) {
        DOMINO_EXIT_WITH_ERROR(ERR_MUTEX_LOCK, "failed to lock counter mutex");
    }
    *count_ptr = counter_count;
    if (mtx_unlock(&counter_mutex) != thrd_success) {
        DOMINO_EXIT_WITH_ERROR(ERR_MUTEX_UNLOCK, "failed to unlock counter mutex");
    }
    return CODE_OK;
}

/** @brief 递增有界业务计数；容量已满时返回 ERR_CAPACITY_EXCEEDED，同步故障直接退出。 */
DOMINO_CODE incrementCounter(void) {
    if (mtx_lock(&counter_mutex) != thrd_success) {
        DOMINO_EXIT_WITH_ERROR(ERR_MUTEX_LOCK, "failed to lock counter mutex");
    }
    DOMINO_CODE result = ERR_CAPACITY_EXCEEDED;
    if (counter_count < UINT64_MAX) {
        counter_count++;
        result = CODE_OK;
    }
    if (mtx_unlock(&counter_mutex) != thrd_success) {
        DOMINO_EXIT_WITH_ERROR(ERR_MUTEX_UNLOCK, "failed to unlock counter mutex");
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
 * @return 成功返回玩家指针；名称无效返回 nullptr，实际分配失败直接报错退出。
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

- 业务时刻和时长统一使用 `domino_runtime_ms_t`（`uint32_t`），表示 32 位累计未冻结现实毫秒。`dominoTimeGetRuntimeMs(&now_ms)` 返回 `void`，正常完成时输出 `runtime_ns / 1000000`；空指针、采样失败、时钟倒退或换算结果超出 `UINT32_MAX` 毫秒时直接打印 stderr 并 exit，不应用游戏日历倍率。冻结期间累计基准不增长，存档保存该基准，加载后继续累计。
- `0` 与 `UINT32_MAX` 均合法；相加前检查 `delta_ms <= UINT32_MAX - base_ms`，相减前确认先后关系，禁止回绕、截断、饱和或归零。约 49.71 个未冻结现实日（按当前日历约 248.55 个游戏年）的累计范围已被接受，不得据此或跨存档累计自行改用、建议改用 64 位业务毫秒。
- time 内部采样和存档基准使用 64 位纳秒保留精度；64 位 UTC 墙上时间戳用于日志及文件命名，消息／计时器 ID 也可为 64 位。这些类型不能当作业务毫秒类型的依据；任务、timer 和收益结算须使用经过范围检查的 32 位毫秒入口。
- timer 延迟与到期时刻、任务持续时间、NPC AI 的处理节奏、阈值时间和收益结算全部使用现实时间单位。游戏日期及日内时分只由显示层按倍率换算，不能作为另一条业务计时轴。
- 时间变量、参数、结构体字段和指标名须明确 `_ns`、`_ms`、`_s` 等单位，换算时只转换现实时间的单位，不把游戏日历倍率混入业务计算。
- 单次游戏会话最多持续 2 个游戏年对应的现实时间，即 24 分钟 × 12 × 2 = 576 个现实分钟（9.6 小时），届时保存退出、再次加载存档继续，是产品要求，当前未实现自动保存退出。单次会话时长包含玩家冻结期间并单独统计；跨存档累计的未冻结现实时间继续增长，不能通过重置累计基准实现会话上限。

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
