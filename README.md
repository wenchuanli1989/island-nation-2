# Domino 游戏内核项目

基于 C23 编写的开放世界模拟内核。当前仓库重点在本地引擎、公共数据结构、日志、导航和 JSON 存档；在线/远程桥接层保留目录骨架，后续再接网络通信。

**版本**: 0.0.1 | **C 标准**: C23 | **构建**: CMake + Clang

---

### 模块结构

```
domino/
├── shared/              # 公共类型、错误码、日志、线程队列及文件/时间/路径工具
├── engine/              # 本地引擎核心，包含 common/host/human/nav/social/storage/time/logger
├── engine-remote/       # 远程引擎桥接层目录，当前仍处于骨架阶段
│   ├── local/           # 计划用于本地代理远程 engine
│   └── server/          # 计划用于在线模式服务端入口
├── client-hub/          # 客户端适配层目录，当前仍处于骨架阶段
└── playground/          # 算法实验和子系统原型验证
```

client-hub 与 engine-remote/local 当前仍只管理各自的日志资源，公开入口也统一为四阶段：

| 模块 | 初始化 | 启动 | 停止 | 销毁 |
| --- | --- | --- | --- | --- |
| client-hub | `dominoClientHubInit()` | `dominoClientHubStart()` | `dominoClientHubStop()` | `dominoClientHubDestroy()` |
| engine-remote/local | `dominoEngineRemoteLocalInit()` | `dominoEngineRemoteLocalStart()` | `dominoEngineRemoteLocalStop()` | `dominoEngineRemoteLocalDestroy()` |

调用方按 Init → Start → Stop → Destroy 管理顺序；仅 Init 的实例也须先 Stop 再 Destroy。Start 只启动线程，Stop 排空日志并 join，Destroy 释放资源；销毁后可重新 Init。这些骨架入口尚未代管 engine 的生命周期，原有一次性运行入口已移除。

引擎对外 Init／Start／Stop／Destroy 严格检查以下状态转换及调用线程，并保留 STARTING／STOPPING／DESTROYING 等协调状态。重复或错序调用在操作资源前直接输出 stderr 并退出，不静默返回，不依赖 assert。受引擎编排的内部模块和辅助调用链遵循资源有效的前置条件，不重复检查已由引擎保证的模块、索引或固定队列初始化条件，也不维护仅为这些重复检查存在的状态或查询接口。可直接调用的公共业务入口仍校验自身访问资格与输入，例如公开道路增删接口保留初始化资格检查。

| 方法 | 允许的进入状态 | 成功后的状态 |
| --- | --- | --- |
| Init | 未初始化 | 已初始化 |
| Start | 已初始化 | 运行中 |
| Stop | 已初始化或运行中 | 已停止 |
| Destroy | 已停止 | 未初始化 |

仅 Init 的引擎 Stop 跳过 human／game_timer Stop、全部 join 和人物结算，仍调用 logger Stop 关闭入队并刷新，然后进入引擎 STOPPED 状态。内部无线程模块遵循 Init → Destroy，nav 由引擎按 Init → InitAfter → DestroyBefore → Destroy 调用；time 无独立 Init，由引擎保证在客户端 DESTROYING 路径中复位快照。这些内部接口不自行校验调用顺序；线程就绪、队列排空、业务参数及真实资源失败检查仍保留。shared logger／queue 可独立于引擎使用，继续保留各自的公共生命周期校验；client-hub 和 engine-remote 的日志包装层直接使用该公共边界。

### 当前引擎生命周期

`dominoEngineInit(launch_config)` 按固定顺序初始化模块：

```text
logger -> main queues -> common -> host -> nav(before) -> social -> human -> game_timer -> storage(load) -> nav(after)
```

- `launch_config.storage_path` 当前必须是非空存档目录；只有根 `meta.json` 不存在时才以新世界启动，其他查询、读取或解析失败均打印 stderr 并退出进程。
- `launch_config.storage_integrity_verify` 控制下次保存是否记录 CRC32C／size；加载使用存档自身声明的校验策略。
- Init 准备日志文件、主循环双向队列、human 执行／规划队列与运行映射表、game_timer 命令队列及同步对象，不创建任何线程；动态业务容量和保存临时缓冲仍按需分配。
- 每次 `dominoEngineInit()` 使用静态初值或上一轮 Destroy 已恢复的空世界基线，再从当前 `storage_path` 加载；上一轮成功 Destroy 后可在同一进程 Init 并切换存档路径，不会继承上一世界的地域、全局 ID、account_id 或 run_mode。Init 成功后时间保持冻结。
- `common` 保存名称/描述字符串表和 ID 映射；`host` 保存地域、岛屿、运动物体和全局自增 ID；`social`、`human` 保存实体数组和 data 扩展数组；`nav` 保存 fork road、road、road line，并在加载后按地域和路网类型重建路径矩阵缓存。

`dominoEngineStart()` 使用 Init 已准备的固定资源，在时钟保持冻结时先启动 logger，再调用 human Start 完成启动校验并建立执行／规划线程，随后启动 game_timer，最后创建消息消费主循环。成功后客户端进入 `RUNNING`，直接解冻时钟并唤醒主循环和业务线程；主循环不负责启动其他线程，也不发布启动回执。executor 仅在首次启动时分批扫描人物，建立初始调度。就绪不表示全部人物已经登记 game_timer、完成规划或启动任务。普通 Freeze 由客户端直接冻结时钟，不发送控制请求或通知业务线程；Thaw 由客户端直接解冻并通知主循环和业务线程，两者均不等待业务线程暂停或恢复回执。各业务线程在批次入口先检查退出、再检查冻结，已经开始的有限批次完整执行；原本等待中的线程保持等待，恢复或退出时再唤醒。恢复直接续接留队消息和已有调度，不再全员扫描。日志与消息队列收发不受冻结影响，冻结成功本身不保证世界已经停止写入或可立即保存。time 模块基于单调时钟累计未冻结的现实时间。业务时刻和时长固定使用 `domino_runtime_ms_t`（`uint32_t`）现实毫秒，毫秒入口校验范围，采样失败或越界时打印 stderr 并退出进程。内部采样及存档基准保留 64 位纳秒精度；UTC 时间戳和 ID 的位宽也不改变业务毫秒的 32 位约定。游戏日历倍率仅用于显示，完整规则见 [AGENTS.md 的时间语义](AGENTS.md#时间语义)。

所有已初始化的世界都由客户端显式按 `dominoEngineStop()` → `dominoEngineDestroy()` 调用。运行过的实例在 Stop 中先进入 `STOPPING`、冻结并校验退出时刻 T，再唤醒主循环和全部业务线程；已经暂停的世界无需解冻。各线程完成当前有限批次后退出，客户端依次 join 主循环、human 执行／规划线程和 game_timer，再将正常人物结算到 T，最后调用 logger Stop 排空日志、join writer 并刷新输出；所有投递者结束前保持目标队列和 game_timer 登记入口有效。Stop 完成后进入 `STOPPED`，保留世界、队列、同步对象、冻结时钟和日志文件，全部线程已退出，尚未保存，也不允许直接 Start 或 Thaw。随后外部调用 Destroy，进入 `DESTROYING`，释放 game_timer 和主循环队列，准备 nav 派生状态并事务式保存存档，再释放 human 的队列及同步对象、其余世界模块，最后关闭日志文件并释放日志队列。Destroy 只接受 `STOPPED`，不负责停止任何线程，未先 Stop 直接调用会报错退出；仅 Init、未 Start 时也必须 Stop → Destroy，此时尚无线程，引擎跳过 human／game_timer Stop、全部 join 和人物结算，仍调用 logger Stop 关闭入队并刷新输出，保留已加载及配置的事实。Destroy 成功后回到未初始化状态，允许同进程建立新世界。普通暂停不遍历人物结算。

Init／Start／Stop／Destroy、Freeze／Thaw 等生命周期接口返回 `void`。无论处于初始化、开始、停止、冻结、解冻、销毁还是运行阶段，致命错误都通过 `DOMINO_EXIT_WITH_ERROR` 直接输出到 stderr，再由 `common.c` 中的公共方法执行 `exit(EXIT_FAILURE)`；标准退出处理仍可执行。不再维护实例首错、`FAILED` 生命周期状态或跨线程故障传播。正常启动就绪、生命周期状态、通知、join 和资源释放顺序继续保留；失败退出不进入正常保存或清理流程。保存失败由后续新进程按磁盘事务恢复。`lib/` 下第三方库代码保持原样，其原有终止行为保留，不纳入项目自有模块的退出实现改造。

业务资格拒绝、查询未命中、队列为空及正常排空结束仍按接口返回业务或控制结果，不统一视为致命错误。运行阶段的非致命错误和正常日志通过日志模块输出；初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断统一使用 `DOMINO_PRINT_MESSAGE` 直接输出 stdout。日志级别不决定是否退出，致命错误必须显式调用公共退出方法。所有等级日志按模块文件、实例默认文件、stdout 的顺序选择唯一输出目标，日志内容不写 stderr；日志队列满时向当前模块的同一输出流同步提示一次并刷新，再通过条件变量等待直到入队。业务队列仍非阻塞入队，内部运行链路无法继续交付必要消息时直接报错退出。logger 在 Init 中以追加模式打开文件并准备队列，Start 创建 writer，Stop 排空队列、join writer 并刷新输出，Destroy 关闭所拥有的文件并释放队列，调用方显式管理顺序；stdout 只刷新、不关闭。

首次 Init 使用静态初值，再次 Init 使用上一轮 Destroy 已复位的空状态；Init 只创建资源、绑定本轮配置和加载数据，不重复清空全局容器、世界计数或模块结构。time 无独立 Init 重置或提前采样，加载只恢复累计基准，Start 解冻时才建立单调起点。通用 queue／logger 首次对象须以静态初值、`={0}` 或 `calloc` 零初始化，Init 在创建资源前检测重复调用；Destroy 后可直接再次 Init。

Destroy 返回前，common、host、social 等模块的容器指针、数量、容量及索引指针均已清空，game_timer 的线程句柄存储和唤醒状态随模块整体复位；引擎配置、路径及线程句柄存储也恢复初始值。世界保存和容器释放完成后调用 `dominoTimeModuleDestroy()`，把内存中的累计基准、单调采样起点及快照版本归零，保持冻结。不依赖下一次 Init 覆盖旧状态，也不改动已保存的累计时间；再次加载同一存档时继续恢复原基准。

### 数据与存档

- 全局实体以 `kvec` 扁平数组保存，`khashl` 维护 `id -> array index` 映射。ID 的 `0` 值保留为无效值，全局自增计数器随存档持久化。
- `DominoHuman` 固定为 64 字节，保存唯一 `human_id`、必需的 data 指针、状态、位置或运动对象引用、地域／空间，以及规划请求、计时登记和就绪去重等调度标识。每个人物独占一份 4096 字节的 `DominoHumanData`，人物与 data 数组同序，共用人物 ID 索引；需求、日程、随机状态及调度补充字段集中在 data 前部，身高体重、情绪、财富、组织关系、建筑与地址等相关属性分组保存。其他核心实体多为 128 字节。结构按自然对齐紧凑排列，`other_info` 补齐预留空间；这一布局尚无缓存性能实测结果。
- storage 先把完整存档写入同父目录 staging 并刷盘，再按 `active -> .prev`、`staging -> active` 的顺序 rename；每次关键目录项变更后都会刷父目录，避免半成品覆盖正式存档。实体按注册表顺序写入 `regions.json` 或 `{type}_{shard_id}.json`，再写 `{type}.meta.json` 和根 `meta.json`。
- 加载 `meta.json` 前会自动恢复未完成的存档事务：只有 `.prev` 时恢复旧存档，active 与 `.prev` 同时存在时保留已提交的 active 并清理旧备份。active、staging 或 `.prev` 为 symlink/普通文件时会拒绝加载或提交，递归清理也只删除 symlink 本身，不进入链接目标。
- 存档事务按单写者设计，同一个 `storage_path` 不允许多个 Domino 进程并发加载或保存。详细状态转换见 [`engine/src/storage/readme.md`](engine/src/storage/readme.md#事务崩溃恢复)。
- `nav` 运行期长期维护 canonical 基础段与追加式增量尾，增删不会触发全量排序；保存前才把逻辑删除的数据排到末尾，并按 `(region_index, road_network_type)` 压实 data segment。路径矩阵和 fork road 指针缓存只存在内存中，不参与序列化。
- 人物日程与需求直接属于 `DominoHumanData`，没有独立 AI 状态／运行态数组或 ID 索引。`task_id_list[32]` 与 `uint8_t task_count` 保存全部持有任务，配合固定三个活动槽位（主通道、次通道 1、次通道 2）；三个通道都接受任意已定义任务类型，每个任务通过持久字段 `channel_index` 选择通道，后续规划与执行按主通道优先、两个次通道同级处理。全局任务池和行为池分别保存 `DominoTask` 与 `DominoBehavior`，各由 ID map 定位。任务用 `behavior_id_list` 保存步骤顺序，以 `current_behavior_id` 定位当前步骤，成功结束后置 0，另保存运行期 `execution_version`。保存时分别写任务事实和全局行为数组，加载时重建两个池的索引，并将每个任务自身的 64 位执行版本初始化为 1；新建任务同样从 1 开始，由执行线程在检查上限后直接递增本任务版本，不要求跨任务唯一。任务与行为 ID 分别由 `DominoGlobalIncrementID.task_increment_id/behavior_increment_id` 原子分配，保持 32 位，回绕时跳过 0 和各自池中仍占用的 ID。规划草案使用独立行为块，store 按值复制后仍由消息或调用方释放草案。人物任务数组按提交顺序追加，删除后稳定前移，计划时间或状态改变不重排；选择任务时按通道、计划时间和业务条件筛选，同时间以数组位置决定先后，不以 ID 大小排序。任务为独立的扁平记录，待启动、运行及未回收终态均保留在数组中；不支持任务暂停、恢复或父子层级。任务失败或取消立即将所属人物置为 `DOMINO_HUMAN_STATUS_EXCEPTION`，执行器跳过该人物，退出时也不再结算；正常成功不会改变人物正常状态。
- 当前存档版本为 22，人物及嵌套 data 使用相同的非零 `human_id`。人物属性、任务 ID 数组、三个活动槽、需求结算时刻及随机状态保存在 `human.entities[].data`；六项需求 `energy/food/water/hygiene/recreation/social` 复用原字段，使用 `uint8_t` 保存 0～100 整数分；自然变化约定按完整未冻结现实分钟结算，未满分钟的时间留待下次，需求结算及单行为睡眠执行器已经接入。JSON `task_id_list` 仅保存有效前缀并保留原顺序，加载由长度恢复 `task_count`。必需的 `human_ai` 模块仅按人物保存 `{human_id, tasks}`，任务保存行为 ID 数组及当前行为 ID；独立 `human_behavior` 模块保存全局行为数组。保存任务时沿人物 ID 数组遍历，加载按 ID 导入可信任务与行为且保留人物数组原序。根 meta 的 `global_increment_id.task/behavior` 分别保存下一候选任务／行为 ID，允许为 0，不表示最后已分配 ID。所有人物调度运行态、任务执行版本、裸指针及预留字段均不写入存档，不兼容旧版本。已定义的任务／行为类型均可保存和加载；睡眠目标有效时长由行为的 `planned_end_ms - planned_begin_ms` 推导，执行期间保持计划区间长度不变，行为记录保存类型、状态、启动标志、计划／实际时间和有效进度；目标建筑、等待检查时刻及一次性效果提交标志按实际业务实现需要再增加。正常人物支持自主睡眠和加载后从保存进度继续执行，异常状态持久保存且不会自动恢复；详细实施边界见 [`human-ai.md`](human-ai.md#2-当前代码与实际改造入口)。
- `playground/sandbox/domino` 已同步版本 22，人物及其 data 的 `human_id=1`，`data.energy=20` 表示 20 分精力，`data.wealth=100`，`task_id_list` 为空数组，三个活动槽全为 0 且任务为空。`data.current_building_id=1` 引用名称为“样例住宅”的建筑，其类型、产权及睡眠资格规则尚未定义；样例不证明该建筑已具备睡眠服务。`data.sleep_address` 保持为 0，它是导航路口 ID，不能填写建筑 ID。

### 第三方库维护策略

`lib/klib/` 只保留 Domino 当前使用的 `kvec.h` 和 `khashl.h`。官方基线为 [attractivechaos/klib](https://github.com/attractivechaos/klib) master，2026-06-04 对比到 commit `97a0fcb790b43b9e5da8994f4671021fec036f19`；官方源码见 [`kvec.h`](https://raw.githubusercontent.com/attractivechaos/klib/97a0fcb790b43b9e5da8994f4671021fec036f19/kvec.h) 和 [`khashl.h`](https://raw.githubusercontent.com/attractivechaos/klib/97a0fcb790b43b9e5da8994f4671021fec036f19/khashl.h)。本仓库版本属于随仓维护版：保留官方数据结构、宏接口和核心算法，只围绕 Domino 的高频内存访问、错误可诊断性和静态检查要求做窄范围补丁。

- `kvec.h`：`kv_*` 扩容通过 `kv_realloc_array()` 检查大小溢出，并在分配失败时输出诊断后 `abort()`。`lib/` 下第三方库代码保持原样；生命周期、存档和运行期均使用原有公开接口，保留库内部的终止行为，不额外增加可失败接口或绕过层。数组布局、增长策略和原公开宏接口保持不变。
- `khashl.h`：保留官方 `r30` 的开放寻址、75% 默认负载因子、Fibonacci hashing、bucket 布局和使用方式。本地只补强错误传播和边界保护：`KHASHL_SET_INIT`/`KHASHL_MAP_INIT` 生成的 `prefix##_resize()` 返回底层 resize 结果；收缩分支先保存 `Krealloc()` 返回值，失败时返回 `-1`，避免直接覆盖原 `keys` 指针；`kh_exist()` 增加 `x < kh_end(h)` 判断，降低错误迭代器导致越界读的风险。
- 静态分析适配：两个头文件包裹 `NOLINTBEGIN`/`NOLINTEND`，屏蔽 klib 宏内部短变量名和多声明风格对 clang-tidy 的噪声，避免第三方宏风格污染项目质量检查。
- 后续同步原则：从官方 klib 更新时优先保留上述本地补丁；如果官方已经覆盖同类修正，再移除本地重复补丁，保持差异可解释、可审计。

### 注意事项
- engine 内部不要把“地域”和“线程”做一对一绑定。当前数据是全局数组，地域只是过滤、分组、调度和存档分段维度；实际并发应由运行时根据对象优先级和批次创建工作任务。


---

### 环境要求

- Linux 目标环境：Ubuntu 26.04 LTS，glibc 2.43+；时间采样依赖 `timespec_get` 的 `TIME_UTC` 和 `TIME_MONOTONIC`。
- CMake 4.2.1+
- Clang 22（C23）
- Docker / Docker Compose（可选，用于远程开发）

---

### macOS 本地调试（当前 Intel Mac）

本地调试入口保留，范围为默认引擎 Playground 的 Debug。当前时间采样要求 SDK 和运行库支持 `timespec_get(TIME_MONOTONIC)`；缺少该能力的 macOS 环境使用上述 Linux/Docker 环境。
满足此条件时，使用支持 C23 的 Clang、CMake 4.2.1+，在仓库根目录执行：

```bash
./scripts/build.sh macos-debug
```

产物和编译数据库位于 `build-macos-debug/`，与 Docker 的 `build-debug/` 分开。
macOS 缺失的 `<threads.h>` 由 [TinyCThread](lib/tinycthread/README.md) 提供，业务代码继续使用标准线程 API。
VS Code 安装 **clangd**、**CodeLLDB** 扩展后，可用 `F12` / `Cmd+点击` 跳转函数定义。
选择 **「调试Playground」** 按 `F5`，会自动构建并调试固定入口 `playground/src/main.c`。

### 构建

**命令行构建**（推荐使用 `scripts/build.sh`）：

```bash
# 调试模式
./scripts/build.sh debug

# 发布模式
./scripts/build.sh release

# 本机极致优化（-march=native，仅限本机运行）
./scripts/build.sh release-native

# 单独构建某个目标，例如：shared, engine, client-hub, playground
./scripts/build.sh debug engine

# 清理
./scripts/build.sh clean
```

**使用独立安装的 glibc 2.43（Linux x86_64）**：

`scripts/build.sh` 仅通过环境变量 `DOMINO_GLIBC_ROOT` 选择独立 glibc；未设置或为空时使用系统默认 glibc，不探测安装目录：

```bash
DOMINO_GLIBC_ROOT="$HOME/.local/opt/glibc-2.43" ./scripts/build.sh debug

# 使用系统默认 glibc
env -u DOMINO_GLIBC_ROOT ./scripts/build.sh debug
```

该目录须包含配套的 glibc 运行库、开发头文件、启动对象、动态加载器、GCC 的 `libgcc_s.so.1`，以及 Linux 内核 ABI 头文件。
当前独立安装来自 Ubuntu 官方 `resolute-updates` 的 `2.43-2ubuntu2.4` 软件包，解包后保留 `usr/include` 和
`usr/lib/x86_64-linux-gnu` 布局，并提供 `lib -> usr/lib`、`lib64 -> usr/lib64` 链接；软件包版本及 SHA256 记录在安装目录的 `installation.json` 中。
安装不替换系统 glibc，也不设置全局 `LD_LIBRARY_PATH`。工具链同时选择编译用 sysroot、ELF 加载器和运行期库搜索路径，生成的程序可直接运行；运行时须保留该安装目录。
改变或取消 `DOMINO_GLIBC_ROOT` 时，构建脚本会自动清理对应目录的编译产物并重建 CMake 配置缓存，重新检测工具链。
系统 glibc 仍须满足上述环境要求；当前宿主机的 glibc 2.39 不支持 `TIME_MONOTONIC`，须指定独立安装的 2.43 才能编译当前源码。

**Playground 构建与运行**：

```bash
# 构建并运行指定 playground 源码
./scripts/build.sh playground-run playground/算法/路径规划/弗洛伊德/main.c

# 仅构建
./scripts/build.sh playground-build playground/算法/路径规划/弗洛伊德/main.c debug
```

**其他构建模式**：

```bash
./scripts/build.sh sanitize    # AddressSanitizer
./scripts/build.sh tsan        # ThreadSanitizer
./scripts/build.sh profile     # gprof 性能分析
```

---

### Codex Cloud

支持 Ubuntu 22.04／24.04／26.04 的 Linux x86_64 环境，需要联网及 root 或免密 sudo。在
[Codex Cloud 环境设置](https://learn.chatgpt.com/docs/environments/cloud-environments)中配置：

| 配置 | 内容 |
| --- | --- |
| Install script（旧版界面为 Setup script） | `bash setup-dev.sh --cloud` |
| Environment variables | `DOMINO_GLIBC_ROOT=/opt/domino/glibc-2.43` |
| Public internet access | 启用 Package managers，并额外允许 `apt.kitware.com`、`apt.llvm.org` |

Cloud 模式复用现有 APT 安装与工具注册流程，按 Ubuntu 发行版选择软件源，跳过远程开发使用的 Git 身份、SSH 和 root 密码配置。
设置 `DOMINO_GLIBC_ROOT` 时，调用 `scripts/setup-glibc.sh` 下载固定版本 `2.43-2ubuntu2.4`，按 `scripts/glibc-2.43-amd64.sha256` 校验后解包至指定目录，复用宿主工具链的 Linux ABI 头文件及 `libgcc_s.so.1`。
请将变量配置在 Cloud 环境设置中，供安装和后续任务共同使用；构建沿用现有独立 glibc 工具链，不替换系统 glibc。

在仓库根目录使用现有命令构建、运行：

```bash
./scripts/build.sh debug
(cd build-debug && ./bin/domino-playground)
```

默认 Playground 完成一次引擎生命周期后退出，存档位于 `build-debug/runtime/sandbox/`；再次运行会加载该存档。

### 开发环境安装（Ubuntu 26.04 服务器）

在 Ubuntu 26.04 服务器上直接执行：

```bash
sudo bash setup-dev.sh
# 如需指定 LLVM 主版本：
sudo LLVM_VERSION=22 bash setup-dev.sh
```

脚本可单独复制到服务器运行，不要求安装 Docker 或下载整个仓库；root 用户可直接执行 `bash setup-dev.sh`。
脚本默认模式仅支持 Ubuntu 26.04，其他系统直接报错；使用系统自带 glibc，通过 APT 安装 CMake、LLVM、调试和分析工具。
脚本沿用原 Dockerfile 的开发环境配置：设置 root 的 Git 身份为 `domino <domino@domino.com>`、创建 SSH 密钥，并启用 root 密码登录，密码为 `123456`。

### 开发环境（Docker + VSCode Remote）

镜像基于 Ubuntu 26.04 LTS，LLVM 22 和 Kitware 软件源均使用 `resolute` 发行版。
Dockerfile 在构建镜像时直接调用 `setup-dev.sh`，与服务器安装使用相同流程。

1. 启动开发环境：`docker compose up -d --build`
2. 连接 VSCode：
   - `Cmd+Shift+P`，输入 "Remote-SSH: Connect to Host"
   - 输入：`root@localhost -p 10022`
   - 密码：`123456`
3. 远程环境建议安装插件：**clangd**、**CodeLLDB**、**clang-format**、**GitLens**

镜像通过 `update-alternatives` 将指定 LLVM 版本注册为 `/usr/bin/clang`、`clangd`、`clang-format` 等无版本号命令。
SSH 会话会重新设置环境，不能仅依赖 Dockerfile 中的 `ENV PATH`，否则 VSCode 的非交互式构建任务可能找不到编译器。
已有容器更新 Dockerfile 或安装脚本后，在宿主机项目目录重新执行 `docker compose up -d --build`，再重新连接 VSCode Remote-SSH。
连接后可执行以下命令验证：

```bash
command -v clang clangd clang-format clang-tidy lldb scan-build
./scripts/build.sh playground-build playground/src/main.c debug
```

---

### VSCode 任务

- **Playground**：对当前 C 文件执行构建或构建并运行（默认）
- **Playground (Debug Build)**：仅构建当前 Playground 文件
- **清理构建**：清理所有 build-* 目录
- **运行 clang-format**：代码格式化
- **运行 clang-tidy**：静态检查

---

### 调试

- 设置断点
- 按 `F5` 或使用调试面板
- 选择配置 **「调试Playground」**（会先执行 Playground 构建任务；macOS 使用上述固定入口）

---

### 代码质量

```bash
./scripts/build.sh format          # 格式化
./scripts/build.sh clang-tidy      # 静态检查
./scripts/build.sh clang-tidy --strict engine   # 严格模式
./scripts/build.sh scan-build      # Clang Static Analyzer
```

---

### 待办

- CI/CD 配置
