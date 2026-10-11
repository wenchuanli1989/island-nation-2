# Domino 游戏内核项目

基于 C23 编写的开放世界模拟内核。当前仓库重点在本地引擎、公共数据结构、日志、导航和 JSON 存档；在线/远程桥接层保留目录骨架，后续再接网络通信。

**版本**: 0.0.1 | **C 标准**: C23 | **构建**: CMake + Clang

---

### 模块结构

```
domino/
├── shared/              # 公共类型、错误码、文件/时间/路径工具
├── engine/              # 本地引擎核心，包含 common/host/human/nav/social/storage/time/logger
├── engine-remote/       # 远程引擎桥接层目录，当前仍处于骨架阶段
│   ├── local/           # 计划用于本地代理远程 engine
│   └── server/          # 计划用于在线模式服务端入口
├── client-hub/          # 客户端适配层目录，当前仍处于骨架阶段
└── playground/          # 算法实验和子系统原型验证
```

### 当前引擎生命周期

`dominoEngineInit(runtime_config)` 按固定顺序初始化模块：

```text
logger -> common -> host -> nav(before) -> social -> human -> storage(load) -> time(after) -> nav(after)
```

- `runtime_config.storage_path` 当前必须是非空存档目录；没有 `meta.json` 时会以新世界启动。
- `runtime_config.storage_integrity_verify` 会写入存档根 `meta.json`，加载时必须与存档声明完全一致。
- 每次 `dominoEngineInit()` 都先建立空世界基线，再从当前 `storage_path` 覆盖加载；同一进程切换存档路径时，不会继承上一世界的地域、全局 ID、account_id 或 run_mode。
- `common` 保存名称/描述字符串表和 ID 映射；`host` 保存地域、岛屿、运动物体和全局自增 ID；`social`、`human` 保存实体数组和 data 扩展数组；`nav` 保存 fork road、road、road line，并在加载后按地域和路网类型重建路径矩阵缓存。

`dominoEngineRun()` 会启动消息消费主循环和 human 行为规划工作线程；time 模块基于单调时钟累计未冻结的现实时间。业务时刻和时长固定使用 `domino_runtime_ms_t`（`uint32_t`）现实毫秒，毫秒入口校验范围并返回错误码。内部采样及存档基准保留 64 位纳秒精度；UTC 时间戳和 ID 的位宽也不改变业务毫秒的 32 位约定。游戏日历倍率仅用于显示，完整规则见 [AGENTS.md 的时间语义](AGENTS.md#时间语义)。

`dominoEngineExit()` 会停止工作线程、冻结并结算时间、准备 nav 派生状态，再事务式保存存档，最后按依赖逆序释放 human、social、nav、host、common、logger。退出阶段返回可恢复错误时引擎保持 `STOPPING`；保存失败不会释放内存世界，处理外部 IO 问题后再次调用 `dominoEngineExit()` 会从未完成阶段继续。内存分配失败按现有接口契约简洁处理，具体规则见 [`CODING_STANDARDS.md`](CODING_STANDARDS.md#41-内存分配失败策略)。`STOPPING` 期间不允许调用其他 public API。

### 数据与存档

- 全局实体以 `kvec` 扁平数组保存，`khashl` 维护 `id -> array index` 映射。ID 的 `0` 值保留为无效值，全局自增计数器随存档持久化。
- `DominoHuman` 固定为 64 字节，保存唯一 `human_id`、必需的 data 指针、状态、位置或运动对象引用、地域／空间，以及规划请求、计时登记和就绪去重等调度标识。每个人物独占一份 4096 字节的 `DominoHumanData`，人物与 data 数组同序，共用人物 ID 索引；需求、日程、随机状态及调度补充字段集中在 data 前部，身高体重、情绪、财富、组织关系、建筑与地址等相关属性分组保存。其他核心实体多为 128 字节。结构按自然对齐紧凑排列，`other_info` 补齐预留空间；这一布局尚无缓存性能实测结果。
- storage 先把完整存档写入同父目录 staging 并刷盘，再按 `active -> .prev`、`staging -> active` 的顺序 rename；每次关键目录项变更后都会刷父目录，避免半成品覆盖正式存档。实体按注册表顺序写入 `regions.json` 或 `{type}_{shard_id}.json`，再写 `{type}.meta.json` 和根 `meta.json`。
- 加载 `meta.json` 前会自动恢复未完成的存档事务：只有 `.prev` 时恢复旧存档，active 与 `.prev` 同时存在时保留已提交的 active 并清理旧备份。active、staging 或 `.prev` 为 symlink/普通文件时会拒绝加载或提交，递归清理也只删除 symlink 本身，不进入链接目标。
- 存档事务按单写者设计，同一个 `storage_path` 不允许多个 Domino 进程并发加载或保存。详细状态转换见 [`engine/src/storage/readme.md`](engine/src/storage/readme.md#事务崩溃恢复)。
- `nav` 运行期长期维护 canonical 基础段与追加式增量尾，增删不会触发全量排序；保存前才把逻辑删除的数据排到末尾，并按 `(region_index, road_network_type)` 压实 data segment。路径矩阵和 fork road 指针缓存只存在内存中，不参与序列化。
- 人物日程与需求直接属于 `DominoHumanData`，没有独立 AI 状态／运行态数组或 ID 索引。`task_id_list[32]` 与 `uint8_t task_count` 保存全部持有任务，配合固定三个活动槽位（主通道、次通道 1、次通道 2）；三个通道都接受任意已定义任务类型，每个任务通过持久字段 `channel_index` 选择通道，后续规划与执行按主通道优先、两个次通道同级处理。全局任务池直接保存 `DominoTask`，每个任务持有独占连续行为块的 `behaviors_ptr` 和运行期 `execution_version`。保存时只写业务字段及行为数组内容，加载时重建指针，并将每个任务自身的 64 位执行版本初始化为 1；新建任务同样从 1 开始，后续由待实现的执行线程在检查上限后直接递增本任务版本，不要求跨任务唯一。任务 ID 统一由 `DominoGlobalIncrementID.task_increment_id` 原子分配，保持 32 位，回绕时跳过 0 和任务池中仍占用的 ID。人物任务数组按提交顺序追加，删除后稳定前移，计划时间或状态改变不重排；选择任务时按通道、计划时间和业务条件筛选，同时间以数组位置决定先后，不以 ID 大小排序。挂起父任务、活动子任务、待交接任务及未回收终态均保留在数组中；日程首尾和任务前后链字段已删除。
- 当前存档版本为 12，人物及嵌套 data 使用相同的非零 `human_id`。人物属性、任务 ID 数组、三个活动槽、需求结算时刻及随机状态保存在 `human.entities[].data`；六项需求 `energy/food/water/hygiene/recreation/social` 复用原字段，使用 `uint8_t` 保存 0～100 整数分；自然变化约定按完整未冻结现实分钟结算，未满分钟的时间留待下次，需求结算及执行器仍待实现。JSON `task_id_list` 仅保存有效前缀并保留原顺序，加载由长度恢复 `task_count`。必需的 `human_ai` 模块仅按人物保存 `{human_id, tasks}` 和嵌套行为，保存任务时沿人物 ID 数组遍历，加载按 ID 校验归属且保留人物数组原序。根 meta 的 `global_increment_id.task` 保存下一候选任务 ID，允许为 0，不再表示最后已分配 ID。所有人物调度运行态、任务执行版本、裸指针及预留字段均不写入存档，不兼容旧版本。已定义的任务／行为类型均可保存和加载；行为专有参数目前包含睡眠目标时长，随具体业务扩充。自主规划、执行回调和暂停后的业务续接仍待实现；详细实施边界见 [`human-ai.md`](human-ai.md#2-当前代码与实际改造入口)。
- `playground/sandbox/domino` 已同步版本 12，人物及其 data 的 `human_id=1`，`data.energy=20` 表示 20 分精力，`data.wealth=100`，`task_id_list` 为空数组，三个活动槽全为 0 且任务为空。`data.current_building_id=1` 引用名称为“样例住宅”的建筑，其类型、产权及睡眠资格规则尚未定义；样例不证明该建筑已具备睡眠服务。`data.sleep_address` 保持为 0，它是导航路口 ID，不能填写建筑 ID。

### 第三方库维护策略

`lib/klib/` 只保留 Domino 当前使用的 `kvec.h` 和 `khashl.h`。官方基线为 [attractivechaos/klib](https://github.com/attractivechaos/klib) master，2026-06-04 对比到 commit `97a0fcb790b43b9e5da8994f4671021fec036f19`；官方源码见 [`kvec.h`](https://raw.githubusercontent.com/attractivechaos/klib/97a0fcb790b43b9e5da8994f4671021fec036f19/kvec.h) 和 [`khashl.h`](https://raw.githubusercontent.com/attractivechaos/klib/97a0fcb790b43b9e5da8994f4671021fec036f19/khashl.h)。本仓库版本属于随仓维护版：保留官方数据结构、宏接口和核心算法，只围绕 Domino 的高频内存访问、错误可诊断性和静态检查要求做窄范围补丁。

- `kvec.h`：官方扩容路径直接调用 `realloc(sizeof(type) * capacity)`；本地统一改为 `kv_realloc_array()`，先检查 `elem_size * count` 是否超过 `SIZE_MAX`，再在分配失败时输出原因、元素大小、元素数量、文件和行号并 `abort()`。这样可以避免容量计算回绕导致短分配，也让 OOM 在调试、压测和大世界数据导入时快速暴露；数组布局、增长策略和公开宏接口保持不变。
- `khashl.h`：保留官方 `r30` 的开放寻址、75% 默认负载因子、Fibonacci hashing、bucket 布局和使用方式。本地只补强错误传播和边界保护：`KHASHL_SET_INIT`/`KHASHL_MAP_INIT` 生成的 `prefix##_resize()` 返回底层 resize 结果；收缩分支先保存 `Krealloc()` 返回值，失败时返回 `-1`，避免直接覆盖原 `keys` 指针；`kh_exist()` 增加 `x < kh_end(h)` 判断，降低错误迭代器导致越界读的风险。
- 静态分析适配：两个头文件包裹 `NOLINTBEGIN`/`NOLINTEND`，屏蔽 klib 宏内部短变量名和多声明风格对 clang-tidy 的噪声，避免第三方宏风格污染项目质量检查。
- 后续同步原则：从官方 klib 更新时优先保留上述本地补丁；如果官方已经覆盖同类修正，再移除本地重复补丁，保持差异可解释、可审计。

### 注意事项
- engine 内部不要把“地域”和“线程”做一对一绑定。当前数据是全局数组，地域只是过滤、分组、调度和存档分段维度；实际并发应由运行时根据对象优先级和批次创建工作任务。


---

### 环境要求

- CMake 4.2+
- Clang（C23）
- Docker / Docker Compose（可选，用于远程开发）

---

### macOS 本地调试（当前 Intel Mac）

本地适配范围为默认引擎 Playground 的 Debug；其他构建和分析模式仍使用 Linux/Docker。
使用系统 Clang、CMake 4.2.1+，在仓库根目录执行：

```bash
./scripts/build.sh macos-debug
```

产物和编译数据库位于 `build-macos-debug/`，与 Docker 的 `build-debug/` 分开；该入口不构建测试目标。
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

### 开发环境（Docker + VSCode Remote）

1. 启动开发环境：`./setup-dev.sh`
2. 连接 VSCode：
   - `Cmd+Shift+P`，输入 "Remote-SSH: Connect to Host"
   - 输入：`root@localhost -p 10022`
   - 密码：`123456`
3. 远程环境建议安装插件：**clangd**、**CodeLLDB**、**clang-format**、**GitLens**

镜像通过 `update-alternatives` 将指定 LLVM 版本注册为 `/usr/bin/clang`、`clangd`、`clang-format` 等无版本号命令。
SSH 会话会重新设置环境，不能仅依赖 Dockerfile 中的 `ENV PATH`，否则 VSCode 的非交互式构建任务可能找不到编译器。
已有容器更新 Dockerfile 后，在宿主机项目目录重新执行 `./setup-dev.sh`，再重新连接 VSCode Remote-SSH。
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

- 单元测试框架
- 代码覆盖率工具
- CI/CD 配置
