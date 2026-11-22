# AGENTS.md

本文件为在本仓库工作的 AI 编码代理（Codex、Claude Code 等）提供指导。

## 项目宪法与任务检查

[项目宪法](doc/项目宪法.md)规定核心约束。本文件只补充实现细则、当前现状和工作流程，不重复宪法条文；两者不一致时以宪法为准，并报告冲突。

- **加载与任务开始：** 加载本文件时必须一并读取项目宪法全文。每次对话任务开始，以及用户新增、调整要求时，都要按输入和拟执行工作的实际语义核对当前宪法，不能只匹配关键词或依赖旧摘要。
- **提前报告冲突：** 执行前指出冲突的用户要求、宪法条款和原因；冲突未解决前不实施该部分，可继续无冲突的工作。宪法未覆盖的需求或实现选择不自动视为冲突。
- **宪法修改权限：** 只有用户在对话中明确要求或授权修改项目宪法，才能在该任务的授权范围内修改。普通实现、修复、重构、文档同步或发现冲突，都不构成修改授权；不得擅改宪法迁就实现。已有明确授权时无需重复确认。
- **任务结束检查：** 最终答复前，对照当前宪法复核本次实际改动和输出内容。存在冲突须说明对应条款、具体位置、原因及处理状态；无冲突则简述检查结果，不以未经授权的宪法修改消除冲突。

## 项目概述

Domino 是基于 C23 的开放世界游戏内核（版本 0.0.1），支持单机离线和在线多人两种模式。它是 CPU 计算密集型模拟引擎，重点关注代码执行速度和内存访问延迟。当前进展集中在本地引擎、公共数据结构、日志、导航、NPC AI 和 JSON 存档；engine-remote 与 client-hub 只有骨架，尚未接入网络通信。

- **验证范围：** 默认只做编译；sanitize、tsan、profile（含 `playground-build`／`playground-run` 的对应模式）只在用户允许时执行。clang-tidy 暂不执行，用户另行通知时再执行。
- **仓库资料：** `doc/ai.zip` 是随仓保存的 AI 研究资料，有意保留，不提议删除、移出或为缩小体积改写 git 历史；被取代的文档直接删除，不建归档目录。`doc/claude-code-guide.html` 是 Claude Code 使用教程，以 Artifact 片段形式保存（没有 doctype），不属于项目规范，不补全其 HTML 骨架。

## 环境与构建

### 环境要求

- Linux（目标环境 Ubuntu 26.04 LTS）、CMake 4.2.1+、Clang 22（C23）、glibc 2.43+（时间采样依赖 `timespec_get` 的 `TIME_UTC` 和 `TIME_MONOTONIC`）。
- **安装脚本：** `scripts/setup-dev.sh` 只负责标准 Ubuntu 26.04／24.04 的项目安装，不含其他环境的配置；其他环境使用单独脚本并尽量复用它，如 `setup-docker.sh`（Docker 开发镜像）、`setup-claude-cloud.sh`。
- **独立 glibc：** 系统 glibc 版本较低时，设置 `DOMINO_GLIBC_ROOT` 指向 `scripts/setup-glibc.sh` 安装的 glibc 2.43，`build.sh` 随之加载工具链文件 `scripts/linux_x86_64_glibc.cmake`；未设置或为空时使用系统 glibc，脚本不探测安装目录。运行产物时须保留该目录。改变或取消该变量时，`build.sh` 会自动清理对应产物并重建 CMake 缓存。安装脚本和工具链只支持 Linux x86_64，其他架构须使用 2.43+ 的系统 glibc。
- **Codex Cloud：** `scripts/setup-codex-cloud.sh` 只支持 Debian 13 x86_64。每个新 shell 先 `source /workspace/.local/domino/env.sh`（启用 PATH 并设置 `DOMINO_GLIBC_ROOT`；安装时设置了 `DOMINO_CLOUD_ROOT` 则为该目录下的 `env.sh`），再使用 `scripts/build.sh`。默认执行沙箱的 ptrace 限制会挡住 LeakSanitizer：运行 sanitize 产物须放宽沙箱再以同一普通用户运行，这类失败不代表代码缺陷。
- **Claude Code 云端：** `scripts/setup-claude-cloud.sh` 只支持 Ubuntu 24.04 x86_64，由环境的 Setup script 以 root 运行，复用 `setup-dev.sh`；glibc 装在 `/opt/glibc-2.43`，`DOMINO_GLIBC_ROOT` 由环境变量配置提供。环境配置和缓存规则见 README 的「Claude Code 云端」。
- **平台：** 目标为 Windows、macOS、Linux、Android、iOS；当前只要求 Linux 编译通过，暂不要求 API 跨平台适配。macOS 只保留 `./scripts/build.sh macos-debug`：在 Intel Mac 上以 Debug 和 `DOMINO_PLAYGROUND_MODE=ON` 构建默认 Playground，产物在 `build-macos-debug/`；SDK 须支持 `timespec_get(TIME_MONOTONIC)`，缺失的 `<threads.h>` 由 `lib/tinycthread` 提供。
- 安装脚本（`scripts/` 下的 `setup-*.sh`）、Docker（`docker/`）及 VS Code 配置见 README 的「独立 glibc」和「开发环境」。

### 构建命令

所有构建通过 `scripts/build.sh` 执行，可执行文件输出到 `build-<preset>/bin/`，静态库输出到 `build-<preset>/static-lib/`。并行数默认为 `nproc`，可用 `CMAKE_BUILD_PARALLEL_LEVEL` 覆盖。

```bash
./scripts/build.sh debug [target]        # 调试模式
./scripts/build.sh release [target]      # 发布模式（-O3、ThinLTO）
./scripts/build.sh release-native        # 发布模式加 -march=native，仅限本机运行
./scripts/build.sh profile               # gprof 性能分析，须用户允许
./scripts/build.sh clean                 # 清理所有 build-* 目录

./scripts/build.sh format engine         # clang-format，必须指定模块或目录
./scripts/build.sh scan-build            # Clang Static Analyzer
./scripts/build.sh clang-tidy [--strict] [target]   # 暂不执行
./scripts/build.sh sanitize              # ASan + UBSan + LSan，须用户允许
./scripts/build.sh tsan                  # ThreadSanitizer（Linux 改用 lib/tinycthread 提供 <threads.h>），须用户允许

# Playground（算法实验、子系统原型验证）
./scripts/build.sh playground-run playground/算法/路径规划/弗洛伊德/main.c
./scripts/build.sh playground-build playground/算法/路径规划/弗洛伊德/main.c debug
```

- **构建目标：** 各构建模式都可追加 `target`：`shared`、`engine`、`engine-remote-local`、`engine-remote-server`、`client-hub`、`playground`，脚本拼成 CMake 目标 `domino-<target>`；省略时构建全部。
- **格式化：** `.clang-format` 基于 Google 风格：4 空格缩进、150 列、大括号不换行、指针星号靠左、头文件自动排序。提交前只格式化改动所在的模块或目录，例如 `format engine`、`format shared`、`format playground/src`；脚本拒绝不带目标的调用。`lib/` 由 `lib/.clang-format` 关闭格式化；目标覆盖 `playground/算法/` 时（如 `format playground`）会改写其中的第三方 `khash.h`，格式化后用 `git status --short -- playground/算法/` 确认它未变。
- **默认 Playground：** 入口为 `playground/src/main.c`。运行 `build-<preset>/bin/domino-playground`，完成一次引擎生命周期后退出；存档按可执行文件位置写在其构建目录的 `runtime/sandbox/`，与工作目录无关，再次运行会加载该存档。
- **实验：** `playground-build`／`playground-run` 以 `DOMINO_PLAYGROUND_MODE=ON` 构建，`PLAYGROUND_SOURCE_DIR` 指向 `playground/` 下含 `main.c` 的实验目录。
- **测试代码：** 当前仓库没有测试代码和模块内部夹具，提交范围见宪法第十一章。不提交的非核心测试代码和临时代码放在根目录 `temp/` 或 `playground/temp/`（后者可用 `playground-build` 构建），由 `.gitignore` 的 `temp/` 规则忽略；不要放进各模块的 `src/`，那里的 `.c` 会被自动收集进正式构建。
- **共用构建目录：** `playground-build`／`playground-run` 复用同名预设的 `build-<preset>/`，`bin/domino-playground` 是最近一次配置的入口；普通构建（如 `debug`）恢复默认入口并关闭 Playground 模式。切换模式会改变全部项目目标的编译定义，等于全量重编。clangd 读取 `build-debug`（macOS 为 `build-macos-debug`）的编译数据库，IDE 诊断也随最近一次配置变化。
- **源文件与夹具：** 除 engine-remote/server 显式列出源文件外，各模块从 `src/` 自动收集 `.c`（`GLOB_RECURSE … CONFIGURE_DEPENDS`），增删源文件无需修改 CMake。名为 `playground.c` 的文件是模块内部夹具，正式构建由顶层 `CMakeLists.txt` 的 `domino_exclude_playground_sources()` 排除；其他文件中的测试代码用 `DOMINO_PLAYGROUND_MODE` 宏条件编译。

## 架构

长期设计目标（模拟流水线阶段、账号与国家交互等）见 [blueprint.md](doc/blueprint.md)；待办与待研究事项见 [todo.md](doc/todo.md)，其中 NPC AI 的阶段执行与延后结构变更仍在评估，未经确认不实现。

### 模块依赖链

子项依赖其上一级：

```
shared（静态库）— 公共类型、错误码、日志、线程队列、读写锁及文件／时间／路径工具
  └─► engine（静态库）— 本地引擎核心
        ├─► engine-remote/local（静态库）— 离线模式桥接层（持久化 + 模块间通信），骨架
        │     └─► client-hub（静态库）— 适配不同上层游戏引擎主程序，骨架
        │           └─► playground（可执行文件）— 测试与实验
        └─► engine-remote/server（可执行文件）— 在线模式服务端，骨架
```

- 所有模块链接 `domino_options`（INTERFACE 库），统一 C23 编译选项和警告配置。
- client-hub 和 engine-remote/local 当前只管理各自的日志资源，公开 `Init／Start／Stop／Destroy` 四阶段入口（如 `dominoClientHubInit()`、`dominoEngineRemoteLocalInit()`），直接使用 shared logger 的公共边界，尚未代管 engine 生命周期。

### 引擎子系统

`engine/src/` 下各子系统通过 `entry.c`／`entry.h` 提供模块入口：

- **common**：名称／描述字符串表和 ID 映射
- **host**：宿主世界，即 AI 实体生活的模拟世界；保存地域、岛屿、运动物体和全局自增 ID
- **human**：人类个体模拟（游戏中唯一的决策单元），见 [NPC AI 实现](#npc-ai-实现)
- **nav**：导航与寻路（Floyd-Warshall），保存 fork road、road、road line
- **social**：社会系统（组织、家庭、国家）
- **storage**：JSON 存档，见[存档](#存档)
- **time**／**game_timer**／**logger**：业务时钟、到期通知、引擎日志接入
- **queue**：引擎内部消息定义；**dispatch**：客户端消息分派骨架

### 数据架构

宿主运行方式见宪法总则，ID、写入归属和结算规则见宪法第九章。

- **扁平存储：** 全局实体以 `kvec` 扁平数组保存，`khashl` 维护 `id → 数组索引` 映射，实现 O(1) 检索。计数器语义因实体而异：动机／任务／行为保存最后已分配 ID（`human/store.c`）；nav fork road 经 `DOMINO_ALLOC_NON_ZERO_ID` 保存下一个待分配 ID。
- **人物布局：** `DominoHuman` 固定 64 字节，保存 `human_id`、必需的 data 指针、运行期调度状态（NORMAL／THINKING，不持久化）、位置或运动对象引用及地域／空间。规划请求资格、唤醒计时器和本地待办放在 human 模块内部、与人物同序的运行数组中，不进入核心结构或存档。每个人物独占一份 4096 字节的 `DominoHumanData`，与人物数组同序并共用人物 ID 索引；AI 字段集中在 data 前部，身高体重、情绪、财富、组织关系、建筑与地址等属性分组保存。其他核心实体多为 128 字节，按自然对齐紧凑排列，由 `other_info` 补齐预留空间。该布局尚无缓存性能实测结果。
- **human 运行期访问：** 读锁接口为 `human/entry.h` 的 `dominoHumanReadLock()`／`dominoHumanReadUnlock()`，读写锁通用实现位于 shared（`domino_shared_rwlock.h`）。规划请求消息只携带 `human_id`、`request_id` 等身份；planner 的读取边界见宪法第六章。
- **模块调用：** `engine/src/entry.c` 通过 `human/entry.h` 编排 human 的生命周期、通知和结算。存档层直接调用 `human/store.h` 导入动机、任务和行为，并通过 `human/state.h` 串行恢复人物容器及索引、遍历数据和绑定引用。这些导入只用于可信加载，不构成客户端任务提交接口。

### 客户端边界

约束见宪法第十章，客户端消息的定义规则见宪法 3.6 节。当前尚未定义任何业务场景：`dominoEngineSendMessage()`／`dominoEngineReceiveMessage()`、主循环和 `engine/src/dispatch/` 只保留通道骨架，`DominoEngineMessage` 只有 `id` 和 `type`。主循环把收到的消息按不支持的类型记录一次并丢弃，不回包。

### 第三方库

- `lib/yyjson/`：JSON 解析；`lib/klib/`：只保留 `khashl.h`（哈希表）和 `kvec.h`（动态数组）；`lib/tinycthread/`：为 macOS 提供 `<threads.h>`；Linux 的 `tsan` 预设经 `DOMINO_USE_TINYCTHREAD=ON` 也改用它，因为 glibc 的 C11 线程函数在 libc 内部直接调用 pthread 实现，ThreadSanitizer 拦截不到，线程启动即崩溃。
- klib 是基于官方 master `97a0fcb7` 的随仓维护版，已带以下本地补丁：
  - `kvec.h`：扩容经 `kv_realloc_array()` 检查大小溢出，分配失败时输出诊断后 `abort()`。
  - `khashl.h`：`prefix##_resize()` 返回底层结果；收缩失败时返回 `-1`，不覆盖原 `keys`；`kh_exist()` 增加 `x < kh_end(h)` 判断。
  - 两个头文件都用 `NOLINTBEGIN／NOLINTEND` 屏蔽 clang-tidy 噪声。

  这些代码按宪法 1.2 节保持原样。项目代码直接使用其公开接口和原有终止行为，不另加可失败接口或绕过层。

## 生命周期实现

### 引擎对外状态

| 方法 | 允许的进入状态 | 协调状态 | 成功后的状态 |
| --- | --- | --- | --- |
| Init | 未初始化 | — | 已初始化 |
| Start | 已初始化 | STARTING | 运行中（RUNNING） |
| Stop | 已初始化或运行中 | STOPPING | 已停止（STOPPED） |
| Destroy | 已停止 | DESTROYING | 未初始化 |

Init 记录客户端线程，其余入口在操作资源前检查状态和调用线程。内部模块接口不自行校验调用顺序：无线程模块只有 Init／Destroy，nav 按 `Init → InitAfter → DestroyBefore → Destroy` 调用。shared logger／queue 可独立于引擎使用，保留各自的公共生命周期校验；首次对象可用静态初值、`={0}` 或 `calloc` 零初始化。进程退出、业务执行和单项资源释放函数按自身语义命名。

### Init

- **调用顺序：** logger → 主循环双向队列 → common → time → host → nav → social → human → game_timer → storage（加载）→ nav InitAfter → human InitAfter。Init 准备日志文件、主循环队列、human 执行器收件箱、三类规划器的十二条队列与运行映射表、game_timer 命令队列及同步对象；动态业务容量和保存用的临时缓冲按需分配。
- **启动配置：** `launch_config.storage_path` 必须是非空存档目录，不能是根目录、`.` 或 `..`；新世界判定见宪法第五章。`storage_integrity_check` 控制下次保存是否记录 CRC32C／size；加载时按存档自身声明的校验策略处理。
- **空世界基线：** 每次 Init 都从静态初值或上一轮 Destroy 复位后的空世界加载。同一进程可以切换存档路径，不继承上一世界的地域、全局 ID、account_id 或 run_mode。
- **加载后准备：** nav InitAfter 重建邻接，并把加载的数据标记为 canonical 布局。human InitAfter 检查任务／行为类型的执行能力，重建计划运行期版本；为每个人物置动机待评估标志（有有效动机的人物再置任务待规划标志），并准备首次本地待办。这一步不推进业务、不前移游标，也不提交规划。

### Start

时钟保持冻结，按 logger → human（执行器，以及动机／任务／行为三个 planner）→ game_timer → 主循环的顺序创建线程，然后进入 RUNNING、解冻时钟并唤醒业务线程。主循环不负责启动其他线程，也不发布启动回执。Start 不做人物扫描和初始调度准备，执行器启动后按有限批次推进人物、投递规划并登记唤醒；Start 返回不代表人物已登记计时、完成规划或启动任务。

解冻后直接续接留队消息和已有调度，不做全员扫描；普通冻结不遍历人物结算。

### Stop

- **已运行的实例：** 进入 STOPPING，冻结时钟并取退出时刻 T，通知 time、主循环队列和业务线程。然后依次 join 主循环、human（执行器、三个 planner）、game_timer，调用 `human/entry.h` 的 `dominoHumanModuleSettle(T)` 统一结算，最后调用 logger Stop。
- **仅 Init 的实例：** 跳过 human／game_timer Stop、全部 join 和人物结算，仍调用 logger Stop 关闭入队并刷新输出。
- **人物结算：** 把全部人物的共同结算游标推进到 T，完成行为终态、行为依赖归并、任务决策及动机计次。单项结果不截断其他结算；结算不启动后续任务、不投递规划、不回收任务。
- **停止后：** 进入 STOPPED，保留世界、队列、同步对象、冻结的时钟和日志文件，供 Destroy 使用。此时尚未保存，也不允许直接 Start 或 Thaw。
- **不跨会话保存的运行态：** 内部计时登记、规划请求、候选、本地待办、计划运行期版本、动机和任务的待规划标志及下次规划时刻。它们由持久化的人物／动机／任务／行为事实以及行为规划重试依据恢复。

### Destroy

- **调用顺序：** game_timer → 主循环队列 → nav DestroyBefore（只规整布局）→ storage（事务式保存）→ human（队列及同步对象）→ social → nav → host → common → time → logger。time 在保存与世界资源清理完成后才销毁。
- **复位范围：** 容器指针、数量、容量和索引，模块状态，线程句柄存储，引擎配置和路径；`run_mode` 恢复为 `local`。

## NPC AI 实现

架构为 动机 → 任务 → 行为 → 动画，设计边界见 [`npc_ai.md`](doc/npc_ai.md)。第一阶段的生产策略只实现恢复精力动机与自主睡眠。通用计时任务／行为（通话、消息、工作）、回应外部信息的动机／任务和插入行为已有执行规则，但第一阶段生产策略不产生它们，外部信息也没有登记入口，目前不会在运行中出现。

信息差和执行器复核遵循宪法第六章。下文的配置字段和容量常量以代码为准，默认值见 `engine/src/human/rules.c` 的 `defaultAiConfig()`。

### 规划线程与请求

- **线程与队列：** 动机、任务、行为各有一个规划线程，复用 `engine/src/human/planner.c` 的私有循环。每个 planner 有高优先级请求、普通请求、高优先级结果、普通结果四条队列，共十二条；每批最多处理 K（`high_quota`）个高优先级请求加 1 个普通请求，单请求计算量受 `compute_budget` 限制。
- **重试延迟：** `retry_delay_ms` 有三种用途：行为规划在无方案、被拒绝或处理后仍需规划时的重试；动机和任务整份候选被拒时的兜底下次规划时刻；第一阶段动机和任务策略 `NO_PLAN` 给出的时刻。
- **优先级：** 已运行任务的修复与插入请求，以及精力不高于 `urgent_energy` 的请求，使用高优先级。配置在 human Init 时固定。
- **请求槽：** 每类规划器在每个人物上一个请求槽，只保存请求 ID、优先级和触发范围，位于人物运行数组，不持久化。规划线程只跳过身份不匹配的请求；投递请求不改变人物唤醒。
- **执行器批次：** 每批最多从六条结果队列各取 `result_quota` 条、从业务收件箱取 `inbox_quota` 条，另处理 `todo_quota` 个本地待办；每个人物每批每类请求至多新投递一次。

### 动机与任务规划

- **动机内容：** 动机只保存方向与历史（类型、状态、实施次数与最近明细、最近失败原因、睡眠需求轮次），不保存行动需要。
- **动机结果：** 动机规划器提出新建（`CREATE`）或终结（`TERMINATE`，带达成／失败／撤销终态）。执行器只复核结构性条件；终结时撤销尚未启动的关联任务，已启动的任务继续执行到结束；动机终结且没有关联任务时回收。
- **任务范围：** 任务规划以人物为范围（`DOMINO_HUMAN_PLAN_SCOPE_HUMAN`），规划器自行决定为哪些动机建任务；每个任务只归属一个有效动机。
- **触发标志：** 动机和任务规划由人物运行态的待规划标志触发。标志只在本会话有效，投递请求时清除，结果不改变标志。
  - 动机待评估标志的置位来源：加载、登记外部信息、任务进入执行终态或被撤销、动机下次规划时刻到达。
  - 任务待规划标志的置位来源：加载（仅有有效动机的人物）、动机结果实际新建或终结了动机、任务进入执行终态或被撤销、登记外部信息、睡眠条件或精力输入实际发生改变、任务下次规划时刻到达。
  - 外部信息、睡眠条件和精力目前都没有输入入口，相应的置位来源暂不会出现。
  - 运行任务启动、让位，以及“没有有效动机”本身，都不置位。
- **下次规划时刻：** 动机和任务规划器随每种结果给出下次规划时刻或“无”（行为恒为无效）。执行器覆盖保存该时刻并并入人物唤醒；整份候选被拒时设为处理时刻 + `retry_delay_ms`。动机和任务没有规划重试，三类规划器都不登记计时器。

### 行为规划

行为规划按 `dominoHumanTaskNeedsBehaviorPlan` 扫描触发，重试状态保留在任务上。规划只询问运行任务和待执行队首：队首带让位请求时先问队首；否则先问运行任务，再为队首提前规划。队首之后的任务等成为队首后再规划。人物唤醒只汇总这两项任务的重试，不维护行为规划等待次序。

### 任务与行为

- **运行任务：** 每个人物同一时间只有一个运行任务（`active_task_id`），其余任务排在一条待执行队列中，队列保存 `task_id`；任务之间没有依赖。运行任务有三个行为通道：主通道放主行为，两个辅助通道放辅助行为，每个通道至多一个当前行为。任务的成功证明和进度只取主行为。
- **ID 与执行部分：** 任务在计划被接受时取得正式 `task_id`，首次启动时才建立执行部分。行为计划是任务独占的值序列，每步指明通道，并带本任务内只增的步骤序号；同一通道内的步骤按先后执行。正式行为 ID 仅在启动时分配。睡眠的工作身份为动机 + 需求轮次。
- **行为依赖：** 同一任务内的行为可以独立，也可以有启动／伴随／完成依赖。每步最多 `DOMINO_BEHAVIOR_DEPENDENCY_COUNT_MAX` 条依赖，只能指向更早的步骤或当前行为；伴随依赖跨通道；不能依赖插入行为。依赖来源结束时，在同一次处理中归并依赖状态并持久化。

### 结算

- **结算游标：** 人物只有 `settled_at_ms` 一个结算游标。自然需求在业务时间（未冻结累计时间）的整分钟边界变化，相位即游标对一分钟取余。行为保留有效进度，周期效果在有效进度的整周期边界发放，相位即有效进度对周期取余。两类相位都不另存。
- **共用计算：** 真实结算与只读预测的共用计算（宪法 9.3）位于 `advance.c`、`needs.c`、`behavior/`。每个边界先结算效果，再判定行为终态并同次归并任务事实，最后逐项调用统一的任务决策（`task/`）。
- **任务成功：** 任务成功必须有自身主行为的成功证明，同次提交动机计次及最近明细（最多 `DOMINO_MOTIVATION_HISTORY_COUNT_MAX` 条）。若仍有插入行为未结束，先记下“完成条件已成立”（持久化标志），等插入行为结束后再提交成功。

### 外部信息与应激

外部信息登记为人物事实（类别、来源、轮次，持久化），当前没有登记入口；以后新增的入口须先按旧值结算到登记时刻，再写入并置动机与任务待规划标志。每种任务类型实现自己的应激规则：

- **接受插入：** 行为规划器为运行任务插入辅助步骤。插入步骤只用辅助通道、没有依赖，不计入成功证明和进度。第一阶段的通用计时任务接受插入。
- **完全让位：** 回应任务的工作身份为外部信息来源 + 轮次，放在队首并带让位请求。回应任务的首批步骤可以启动时，运行任务才取消全部当前行为、清空未来步骤，保持 OPEN 状态回到队首；回应任务结束后，原任务按原身份恢复。第一阶段的睡眠完全让位。

### 候选与容量

- **候选：** 每份候选独占一块有界连续内存，建议数、步骤数和字节数上限见 `engine/src/queue/message.h` 的 `DOMINO_PLAN_CANDIDATE_*`，候选总预算见 `rules.h` 的 `DOMINO_HUMAN_CANDIDATE_BUDGET_BYTES`。必要部分整体预检，可选项按顺序尝试；正式记录复制值后整块释放。计划版本和队列版本只在本会话有效。
- **容量：** 世界人数及全局动机／任务／计划项／行为记录上限见 `rules.h` 的 `DOMINO_HUMAN_*_MAX`；每人物动机、任务上限及单任务“未来步骤 + 未回收行为记录”上限见 `shared/include/domino_shared_human_task.h`。业务队列容量见 `domino_shared_queue.h` 的 `DOMINO_THREAD_QUEUE_*_CAPACITY`；logger 队列初始容量与最大容量相同、运行中不扩容；十二条规划队列的最大槽位内存由 `planner.c` 的静态断言限制。

### 测试与验证

当前没有 NPC AI 的测试代码和夹具。[npc_ai.md 第 12.4 节](doc/npc_ai.md#124-验收清单)列出逐项验收要求和任务完成边界场景，改动 NPC AI 时据此验证；测试代码的提交范围见宪法第十一章，测试注入的业务输入见宪法第十章。

## 存档

原子提交、单写者、新世界判定和版本规则见宪法第五章。

- **事务提交：** storage 先把完整存档写入同父目录的 staging 并刷盘，再按 `active → .prev`、`staging → active` 的顺序 rename；每次关键目录项变更后都刷父目录。实体按注册表顺序写入 `regions.json` 或 `{type}_{shard_id}.json`，然后写 `{type}.meta.json`，最后写根 `meta.json`。实现见 `engine/src/storage/transaction.c`。
- **加载恢复：** 加载根 `meta.json` 前自动恢复未完成的事务：只有 `.prev` 时恢复旧存档；active 与 `.prev` 同时存在时保留 active 并清理备份。active、staging 或 `.prev` 是 symlink 或普通文件时，拒绝加载或提交；递归清理只删除 symlink 本身，不进入链接目标。保存失败的存档由后续新进程按磁盘事务恢复。
- **nav 布局：** 运行期维护 canonical 基础段与追加式增量尾，增删不触发全量排序。DestroyBefore 才把逻辑删除的数据排到末尾，并按 `(region_index, road_network_type)` 压实分段；fork road 与 road 的模块 meta 用 `nav_segments` 记录各分组的基础段和增量尾。路径矩阵和 fork road 指针缓存只在内存中，不序列化。
- **当前版本 32**（`DOMINO_STORAGE_SAVE_VERSION`）。实体的 JSON key 与结构体字段同名，派生值（如 `sleep_forbidden`、名称和描述字符串）除外。各文件内容：
  - 根 `meta.json`：`version`、`integrity_check`、`account_id`、`run_mode`、`runtime_base_ns`（累计未冻结纳秒）、`id_counters` 和各模块 meta 的摘要 `modules`。`id_counters` 中 `last_*_id`（动机、任务、行为）为最后已分配 ID，`next_*_id` 为下一个待分配 ID。
  - `{type}.meta.json`：`type`、`total_count`、`shard_count` 和分片摘要 `shards`。
  - `human.entities[].data`：结算游标、睡眠条件、显示称呼、待处理外部信息（类别、来源、轮次）、动机／任务清单、运行任务和待执行队列。
  - `human_motivation`：身份、类型、生命周期（终态记在 `status` 中，已回收的动机不再保存）、需求轮次与承接任务、累计实施次数、最近失败原因，以及按时间顺序的最近成功明细。
  - `human_task`：工作身份、动机引用、回应的外部信息类别、完成指标、截止点、最后已分配的步骤序号、未来行为计划值（步骤序号、通道、插入标志、行为依赖）、行为规划重试依据、让位请求、“完成条件已成立”标志，以及可选的执行部分（实际时刻、各通道当前行为、最近行为、主行为成功／失败／取消计数、归并进度和结束原因）。
  - `human_behavior`：步骤序号、通道、插入标志、行为依赖、类型、状态、实际时刻、目标有效时长、有效进度和结束原因。

## 时间语义

计时规则见[宪法第七章](doc/项目宪法.md#七游戏时间原则)。接口位置：

- **业务时间：** `domino_runtime_ms_t` 定义于 `shared/include/domino_shared_types.h`；业务通过 `engine/src/time/entry.h` 的 `dominoTimeGetRuntimeMs(&now_ms)` 读取，该接口返回 `void`。
- **冻结等待：** `dominoTimeModuleGetFreezeEpoch()` 返回 `uint64_t`，观察到的值原样传给 `dominoTimeModuleWait()`；仅供显示的等待计数用 `dominoTimeModuleGetFreezeWaiterCount()` 读取；退出时用 `dominoTimeModuleWakeAll()` 通知 time 等待者。

## 队列消息结构

结构规则见宪法 3.3、3.6 节。当前各队列使用的结构：

- **计时命令队列：** `DominoGameTimerRequestMessage`
- **三类规划器：** 六条请求队列使用 `DominoHumanPlanRequestMessage`，六条结果队列使用 `DominoHumanPlanResultMessage`。候选扁平块布局（公共头及动机／任务／行为条目）同样定义在 `engine/src/queue/message.h`。
- **human 执行器收件箱：** `DominoHumanInboxMessage`，只承载 GameTimer 到期通知（队列 ID + 按值保存的计时记录）。
- **客户端收发队列：** `DominoEngineMessage`，见[客户端边界](#客户端边界)。

## 编码规范

**语言规则：**

- C23 标准，使用 Clang 编译；线程与原子操作 API 见宪法总则。
- **编译警告：** 项目目标经 `domino_options` 启用 `-Wall`、`-Wextra`、`-Wpedantic`、`-Wconversion`、`-Wsign-conversion` 等警告，构建保持零警告，改动不得引入新警告。缺少原型或声明按错误处理，并启用 `-fno-common`：非 `static` 函数须先在头文件声明，头文件只能用 `extern` 声明全局变量。
- **配置差异：** Debug 使用 `-O0` 和 `-ftrapv`（有符号整数溢出时陷入）；Release 使用 `-O3`、ThinLTO 和 `-DNDEBUG`，`assert()` 不生效。
- 使用固定宽度整数类型（`uint32_t`、`int16_t` 等）。
- **函数注释：** 使用 Doxygen 风格，按需书写，不要求每个函数配齐全部标签。
  - 头文件声明的函数必须有 `@brief`，写清功能及调用契约；前置条件用 `@pre`，不做的事和副作用用 `@note`。
  - `@param`／`@return` 只在名称和类型说不清时写：单位与范围、是否可空、输出参数、所有权、各返回值的含义；不写复述类型或名称的条目。
  - `static` 函数在以下情况写 `@brief`：逻辑不平凡、返回值含义不明显（如 `bool` 表示拒绝还是跳过）、修改输出参数或有锁／线程要求。几行的取值或转发函数不写。
  - 注释说明“为什么”和契约，不复述代码在做什么；修改函数签名或行为时同步注释，过时注释按缺陷处理。

**命名规范：**

- 文件：`snake_case.c` / `snake_case.h`
- 函数：`camelCase`，动词开头（如 `createPlayer`、`navModuleInit`）
- 变量：`snake_case`，带语义后缀（`_count`、`_size`、`_len`、`_num`、`_index`、`_max`、`_min`、`_default`、`_ptr`、`_str`、`_buf`、`_flag`）；时间量的单位后缀见宪法 7.1 节
- 全局变量：跨文件共享的加 `g_domino_` 前缀（如实体容器 `g_domino_human_list`、ID 映射表 `g_domino_human_id_map`），文件内 `static` 的加 `g_` 前缀；khashl 生成函数的前缀（如 `dominoHumanIdMap_get`）不得与变量同名
- 结构体／联合体：`PascalCase`（如 `DominoPlayer`）
- 宏、枚举类型及枚举值：`UPPER_SNAKE_CASE`（如 `DOMINO_TASK_TYPE`、`DOMINO_TASK_TYPE_SLEEP`）
- 指针声明：星号靠左（`int* ptr`），每个指针变量单独声明
- 类型别名：`lowercase_t` 后缀（如 `domino_human_id_t`）
- 模块内部函数和变量用 `static` 限定作用域

**接口与错误处理：**

- 业务与控制接口成功时返回 0 或正值，其他结果按各自约定。没有可恢复业务结果的生命周期及冻结／解冻接口返回 `void`。
- `DOMINO_EXIT_WITH_ERROR`、`DOMINO_PRINT_MESSAGE` 定义于 `shared/include/domino_shared_common.h`，使用场景见宪法 4.6 节。致命退出由 shared common 执行 `exit(EXIT_FAILURE)`，标准退出处理仍会执行。
- `assert()` 只用于内部不变量检查。
- 正常销毁及可恢复操作可以用 `goto` 集中清理。不为规避未来的分配失败而提前扩容、占位或增加降级逻辑。
- 公共工具保留头文件约定的输入、输出和返回结果。成功调用会完整写入的局部输出无需预清零。格式化函数的返回值仍须检查；文件状态在磁盘操作后按需重读。
- 优先使用容器的公开接口，不操作内部字段绕过接口。容量、参数、格式、IO 错误及零长度请求按接口语义处理，不用空指针未定义行为代替错误处理。
- 格式化输出使用固定格式字符串，不把外部输入当作格式串；有意未使用的参数用 `(void)name` 标记。
- 需要同时持有多把锁时按固定顺序获取，避免死锁。

**头文件结构：**

- 公共 API 头文件位于 `<module>/include/`（如 `domino_engine.h`）。
- 模块内部头文件与源文件同级放置（如 `engine/src/host/entry.h`）。
- 包含顺序：系统头文件 → 第三方库 → 项目头文件，由 clang-format 强制排序。

**内存与性能：**

- 结构体成员按大小降序排列，减少对齐填充。
- 小型工具函数使用 `static inline`。
- 热点路径函数标记 `__attribute__((hot))`。
- 网络数据包大小控制在 MTU 范围内。
