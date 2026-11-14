# AGENTS.md

本文件为 Codex (Codex.ai/code) 在本仓库中工作时提供指导。

## 项目宪法与任务检查

[项目宪法](doc/项目宪法.md)规定核心约束，本文件补充实现细则；两者不一致时，以宪法为准并报告冲突。

- **加载与任务开始：** 加载本文件时必须一并读取项目宪法全文。每次对话任务开始及用户新增、调整要求时，按输入和拟执行工作的实际语义核对当前宪法，不能仅匹配关键词或依赖旧摘要。
- **提前报告冲突：** 执行前指出冲突的用户要求、宪法条款和原因；冲突未解决前不实施该部分，可继续无冲突工作。宪法未覆盖的需求或实现选择不自动视为冲突。
- **宪法修改权限：** 只有用户在对话中明确要求或授权修改项目宪法，才可在该任务的授权范围内修改。普通实现、修复、重构、文档同步或发现冲突均不构成修改授权；不得擅改宪法迁就实现。已有明确授权无需重复确认。
- **任务结束检查：** 最终答复前，对照当前宪法复核本次实际改动和输出内容。存在冲突须明确说明对应条款、具体位置、原因及处理状态；无冲突则简述检查结果，不以未经授权的宪法修改消除冲突。

## 项目概述

Domino 是一个基于 C23 编写的开放世界游戏内核，支持单机离线和在线多人两种模式。这是一个 CPU 计算密集型模拟引擎，重点关注代码执行速度和内存访问延迟。

## 构建命令

所有构建通过 `scripts/build.sh` 执行，需要 CMake 4.2+ 和 Clang。

```bash
./scripts/build.sh debug                # 调试模式构建
./scripts/build.sh debug engine          # 构建单个目标（shared, engine, client-hub, playground）
./scripts/build.sh release               # 发布模式构建（-O3, LTO）
./scripts/build.sh clean                 # 清理所有 build-* 目录

# 代码质量
./scripts/build.sh format               # clang-format 格式化所有 .c/.h 文件
./scripts/build.sh format shared         # 格式化指定模块
./scripts/build.sh clang-tidy            # 静态分析
./scripts/build.sh clang-tidy --strict engine  # 严格模式（发现错误时退出）
./scripts/build.sh scan-build            # Clang Static Analyzer

# Sanitizer
./scripts/build.sh sanitize              # AddressSanitizer + UBSan + LeakSanitizer
./scripts/build.sh tsan                  # ThreadSanitizer

# Playground（算法实验、子系统原型验证）
./scripts/build.sh playground-run playground/算法/路径规划/弗洛伊德/main.c
./scripts/build.sh playground-build playground/算法/路径规划/弗洛伊德/main.c debug
```

构建输出位于 `build-<preset>/bin/` 和 `build-<preset>/lib/`。

### Codex Cloud

Debian 13（trixie）x86_64 的独立安装入口为 `bash setup-codex-cloud.sh`，普通用户即可执行，默认安装到 `/workspace/.local/domino`；可用 `DOMINO_CLOUD_ROOT` 指定其他可写绝对路径（不含空白、双引号或反斜杠）。
安装通过独立 APT 配置校验、下载并解包 Debian backports 的 CMake、Clang 22 及依赖，不修改系统配置；独立 glibc 由 `scripts/setup-glibc.sh` 准备。每个新 shell 先 `source /workspace/.local/domino/env.sh`，启用 PATH 并设置 `DOMINO_GLIBC_ROOT`，再使用 `scripts/build.sh`。配置与运行命令见 README 的 Codex Cloud 小节。

## 架构

### 模块依赖链

```
shared（静态库）
  └─► engine（静态库）— 游戏逻辑子系统
        └─► engine-remote/local（静态库）— 离线模式桥接层（持久化 + 模块间通信）
        └─► engine-remote/server（可执行文件）— 在线模式服务端
              └─► client-hub（静态库）— 适配不同上层游戏引擎主程序
                    └─► playground（可执行文件）— 测试与实验
```

所有模块链接 `domino_options`（INTERFACE 库），以统一 C23 编译选项和警告配置。

### 引擎子系统

引擎（`engine/src/`）包含以下子系统，各子系统通过 `entry.c`/`entry.h` 提供模块入口：

- **host** — 宿主世界，AI 实体生活的模拟世界
- **human** — 人类个体模拟（游戏中唯一的决策单元）
- **nav** — 导航与寻路（Floyd-Warshall 算法实现）
- **social** — 社会系统（组织、家庭、国家）

### 生命周期实现约定

生命周期流程、阶段职责和校验原则遵循项目宪法，具体实现约定如下：

- 不保留旧生命周期名称或兼容别名；进程退出、业务执行和单项资源释放函数按自身语义命名。
- shared logger／queue 保留独立公共契约；首次对象须使用静态初值、`={0}` 或 `calloc`，销毁复位后可直接再次初始化。
- 启动线程顺序：logger → human 执行器 → 动机／任务／行为三个 planner → game_timer → 主循环；全部创建完成后进入 RUNNING，再解冻并唤醒业务线程。
- 引擎 Init 在 storage 加载及引用绑定后调用 nav／human 的 InitAfter：导航恢复邻接，human 检查活动任务执行能力并重建首次唤醒，不推进业务或提交规划。Start 不再承担人物扫描和初始调度准备。
- 停止时依次 join 主循环、human 执行器、动机／任务／行为三个 planner、game_timer，再通过 `human/entry.h` 的 `dominoHumanModuleSettle()` 统一结算，最后排空并 join logger。结算保留在引擎 Stop 阶段，DestroyBefore／Destroy 不重复结算。仅 Init 的实例由引擎跳过 human／game_timer Stop、全部 join 和结算，仍调用 logger Stop 关闭入队并刷新。两条路径均保留实例资源供 Destroy 使用。
- time 的接入位置：引擎在 storage 加载前调用 `dominoTimeModuleInit()`，在保存与世界资源清理完成后调用 `dominoTimeModuleDestroy()`。
- Destroy 不调用 Stop 或 join；复位范围包括容器指针／数量／容量、索引、模块状态、线程句柄存储、引擎配置和路径。
- Stop 统一结算全部人物、活动通道及自然需求，单项失败不截断其他结算，不启动后续任务；Nav 的 DestroyBefore 只规整布局。内部计时登记和未提交规划不跨会话保存，由持久化人物／动机／任务／行为事实恢复，验证覆盖留队及在途工作。

### 模拟流水线设计目标

模拟逻辑计划按顺序组织为以下阶段：
1. **物理阶段** — 物体运动、空间分布、环境模拟（气候、地形、水文）
2. **个体生物智能阶段** — 人类、动物
3. **群体生物智能阶段** — 家庭、城市、国家、公司、帮派、宗教组织
4. **超级智能阶段** — 观察/学习/预测玩家行为，增强游戏体验

每个阶段对目标对象进行综合排序和过滤，多线程分批次处理，并产生事件消息供后续阶段使用。

### 数据架构

- 全局数据以扁平数组存储；哈希表（khashl）将实体 ID 映射到数组索引，实现 O(1) 检索
- **客户端任务边界：** 客户端不得直接提交任务请求、规划请求或规划结果；任务决策、规划与提交由引擎内部完成。不得为了测试增加引擎对外任务提交方法或消息类型；验证使用模块内部夹具、存档场景或引擎自主调度。
- human 运行期由执行器独占写入 Human／Data／Motivation／Task／Behavior、容器及索引；引擎内部其他线程通过 `human/entry.h` 持读锁读取，修改按 ID 和值提交消息。读写锁通用实现位于 shared；加载、全部业务线程结束后的结算、保存和销毁串行直访，不加读写锁。冻结及 STOPPING 的末批仍遵守运行期访问约定。
- 引擎入口的模块调用遵循[项目宪法第八章：模块调用规范](doc/项目宪法.md#八模块调用规范)：`engine/src/entry.c` 通过 `human/entry.h` 编排 human 的生命周期、通知和结算；模块间调用不强制经过 `entry.h`。存档层直接调用 `human/store.h` 导入动机、任务和行为，通过 `human/state.h` 串行恢复人物容器及索引、遍历数据和绑定引用；导入仅用于可信加载，不构成客户端任务提交接口。
- planner 可在同一次 human 读锁范围内直接查询人物及关联 Task／Behavior；规划请求队列只保存 `human_id`、`request_id` 等必要身份，不搬运人物数据。需要解锁后计算时仅复制必要局部值，不跨解锁保留借用指针；规划结果仍交执行器复核并写入。
- 宿主世界设计为只能运行在单一物理机器上（支持离线运行）
- 第三方库：**yyjson**（JSON 解析器，位于 `lib/yyjson/`）、**klib**（`khashl.h` 哈希表、`kvec.h` 动态数组，位于 `lib/klib/`）

### NPC AI 实现

- 第一阶段实现恢复精力动机和自主睡眠策略；详细边界见 `npc_ai.md`。动机、任务、行为各有一个规划线程和高／普通请求、结果四条队列，共十二条；三线程复用私有循环，分别按 K 个高请求加 1 个普通请求处理。默认 K=4、请求期限 3000ms、计算预算 512；修复及精力不高于 10 的请求使用高优先级。
- 每人物三个请求槽只保存请求 ID、优先级、截止点和触发范围；任务范围区分新建工作的动机身份与已接受任务 ID。执行器每批最多处理六条结果队列各 8 条、业务收件箱 16 条，并另处理 16 个本地待办。
- 任务在计划接受时获得正式 ID，首次启动才建立执行部分；行为计划是独占值序列，正式行为 ID 仅在启动时分配。三个执行队列和活动槽保存任务 ID；正式动机／任务／行为 ID 以存档世界内最后已分配值递增，耗尽致命退出，永不复用。
- 人物只有 `settled_at_ms` 一个结算游标；自然需求相位、行为有效进度和效果相位分别保留。结算和有界只读预测共用效果计算及任务决策；先完成全部通道效果，再归并行为终态，最后按依赖拓扑处理任务结果。任务成功须有自身成功行为证明，动机在同次提交中计次并保留最近 10 条实施历史。
- 候选每份独占一块有界连续内存：最多 8 个建议、255 步、64KiB；必要部分整体预检，可选项按顺序尝试，正式记录复制值后整块释放。未来行为计划版本、任务安排版本及通道版本仅在对应事实变化时递增，不保存到存档。
- 业务队列默认初始 20000、最大 100000；logger 默认固定 20000。十二条规划队列合计最大槽位内存以静态断言限制在 64MiB 内。
- 世界上限 10000 人；每人物最多 8 个动机、32 个任务，每任务未来步骤与未回收行为合计最多 255。全局动机／任务／行为计划／行为记录上限分别为 80000／320000／1000000／1000000，候选总内存预算 256MiB。
- 客户端只支持概况查询与有版本条件的称呼修改。主循环持短读锁复制查询值；执行器校验管理身份并提交修改。会话 ID 不跨实例复用，查询／改名不触发 AI。内部睡眠条件和精力更新先按旧值结算，再提交输入及统一决策。
- 当前存档版本 26，拒绝旧格式。验证入口为 `playground/npc_ai/main.c`，支持 `scripts/build.sh playground-run playground/npc_ai/main.c debug|release|sanitize|tsan`。

### 队列消息结构

消息结构、命名及定义位置遵循[项目宪法第 3.6 节](doc/项目宪法.md#36-消息结构与定义位置)，队列 ID 与业务身份遵循第 3.3 节。

当前计时命令队列直接使用 `DominoGameTimerRequestMessage`，规划请求队列直接使用 `DominoHumanPlanRequestMessage`，human 执行器混合收件箱使用 `DominoHumanExecutionMessage`，客户端收发队列复用 `DominoEngineMessage`。

### 时间语义

计时口径、数值范围、日历显示、冻结／解冻、等待计数及会话限制统一遵循[项目宪法第七章：游戏时间原则](doc/项目宪法.md#七游戏时间原则)。本节只补充实现约定：

- **业务入口：** `domino_runtime_ms_t` 定义于 `shared/include/domino_shared_types.h`；业务通过 `engine/src/time/entry.h` 的 `dominoTimeGetRuntimeMs(&now_ms)` 读取时间，接口返回 `void`。
- **业务算术：** 加法前检查 `delta_ms <= UINT32_MAX - base_ms`，减法 `now_ms - base_ms` 前确认 `now_ms >= base_ms`，失败按宪法的错误规则处理。
- **冻结接口：** `dominoTimeModuleGetFreezeEpoch()` 返回 `uint64_t`，观察值原样传给 `dominoTimeModuleWait()`；显示计数通过 `dominoTimeModuleGetFreezeWaiterCount()` 读取，退出时通过 `dominoTimeModuleWakeAll()` 通知 time 等待者。

### Playground

`playground/` 用于算法实验和子系统原型验证。每个实验独立存放于各自目录下，包含 `main.c`。构建系统可将 `PLAYGROUND_SOURCE_DIR` 指向 `playground/` 下的任意子目录。在 `DOMINO_PLAYGROUND_MODE` 模式下，各模块可通过 `playground.c` 直接测试内部实现逻辑。

## 编码规范

**语言规则：**
- C23 标准，使用 Clang 编译
- 多线程统一使用 C23 标准 API：`<threads.h>` 的 `thrd_*`、`mtx_*`、`cnd_*`、`tss_*`、`call_once`，原子操作使用 `<stdatomic.h>`。项目自有代码不直接使用 pthread 或操作系统线程 API，也不将标准同步对象强转为原生类型；第三方库和标准库底层实现不在此限制范围内。
- 使用固定宽度整数类型（`uint32_t`、`int16_t` 等）
- 平台目标：Windows、macOS、Linux、Android、iOS；当前仅要求 Linux 编译通过，暂不要求 API 跨平台适配
- 使用 Doxygen 风格注释（`@brief`、`@param`、`@return`）

**命名规范：**
- 文件：`snake_case.c` / `snake_case.h`
- 函数：`camelCase`，动词开头（如 `createPlayer`、`navModuleInit`）
- 变量：`snake_case`，带语义后缀（`_count`、`_size`、`_len`、`_ptr`、`_str`、`_buf`、`_index`、`_flag`）
- 结构体/联合体：`PascalCase`（如 `DominoPlayer`）
- 宏/枚举值：`UPPER_SNAKE_CASE`
- 指针声明：星号靠左（`int* ptr`）
- 类型别名：`lowercase_t` 后缀（如 `domino_human_id_t`）

**接口与错误处理细则：**
- 业务与控制接口成功返回 0 或正值，其他结果遵循各自约定；无可恢复业务结果的生命周期及冻结／解冻接口返回 `void`。
- 致命错误统一调用 `DOMINO_EXIT_WITH_ERROR`，不等待日志排空，由 shared common 执行 `exit(EXIT_FAILURE)`，允许标准退出处理；生命周期及冻结／解冻的非致命诊断使用 `DOMINO_PRINT_MESSAGE`。
- `assert()` 仅用于内部不变量检查。
- 业务日志按模块文件、实例默认文件、stdout 的顺序选择输出目标。按操作所属路径区分输出通道，不在 logger 中增加阶段标志。
- 正常销毁及可恢复操作可使用 `goto` 集中清理；不为规避未来分配失败提前扩容、占位或增加降级逻辑。
- 公共工具保留头文件约定的输入输出及返回结果；成功调用会完整写入的局部输出无需预清零。格式化返回值仍须检查，文件状态在磁盘操作后按需重读。
- 优先使用容器公开接口，不操作内部字段绕过接口。容量、参数、格式、IO 错误及零长度请求按接口语义处理，不用空指针未定义行为代替错误处理。

**头文件结构：**
- 公共 API 头文件位于 `<module>/include/`（如 `domino_engine.h`）
- 模块内部头文件与源文件同级放置（如 `engine/src/host/entry.h`）
- 包含顺序：系统头文件 → 第三方库 → 项目头文件（由 clang-format 强制排序）

**内存与性能：**
- 结构体成员按大小降序排列，减少内存对齐填充
- 小型工具函数使用 `static inline`
- 热点路径函数标记 `__attribute__((hot))`
- 网络数据包大小控制在 MTU 范围内

## 代码格式化

通过 `.clang-format` 配置（基于 Google 风格）：4 空格缩进、150 列宽度限制、大括号不换行、指针星号靠左、头文件自动排序。提交前执行 `./scripts/build.sh format`。

## 注意事项
- 当前项目是完全独立新项目，重构时不要考虑历史兼容性问题，为了达到最佳效果，可以调整已有函数定义或者全局变量，不要被现有代码限定影响发挥
