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

`dominoEngineRun()` 会启动消息消费主循环和 human 行为规划工作线程；time 模块基于单调时钟维护游戏时间。

`dominoEngineExit()` 会停止工作线程、冻结并结算时间、准备 nav 派生状态，再事务式保存存档，最后按依赖逆序释放 human、social、nav、host、common、logger。任一退出阶段失败时引擎保持 `STOPPING`；保存失败不会释放内存世界，处理外部 IO 问题后再次调用 `dominoEngineExit()` 会从未完成阶段继续。`STOPPING` 期间不允许调用其他 public API。

### 数据与存档

- 全局实体以 `kvec` 扁平数组保存，`khashl` 维护 `id -> array index` 映射。ID 的 `0` 值保留为无效值，全局自增计数器随存档持久化。
- 结构体按固定尺寸设计：核心实体多为 128 字节，扩展 data 多为 4096 字节；`other_info` 字段通常是运行时/预留空间，不进入 JSON 存档。
- storage 先把完整存档写入同父目录 staging 并刷盘，再按 `active -> .prev`、`staging -> active` 的顺序 rename；每次关键目录项变更后都会刷父目录，避免半成品覆盖正式存档。实体按注册表顺序写入 `regions.json` 或 `{type}_{shard_id}.json`，再写 `{type}.meta.json` 和根 `meta.json`。
- 加载 `meta.json` 前会自动恢复未完成的存档事务：只有 `.prev` 时恢复旧存档，active 与 `.prev` 同时存在时保留已提交的 active 并清理旧备份。active、staging 或 `.prev` 为 symlink/普通文件时会拒绝加载或提交，递归清理也只删除 symlink 本身，不进入链接目标。
- 存档事务按单写者设计，同一个 `storage_path` 不允许多个 Domino 进程并发加载或保存。详细状态转换见 [`engine/src/storage/readme.md`](engine/src/storage/readme.md#事务崩溃恢复)。
- `nav` 运行期长期维护 canonical 基础段与追加式增量尾，增删不会触发全量排序；保存前才把逻辑删除的数据排到末尾，并按 `(region_index, road_network_type)` 压实 data segment。路径矩阵和 fork road 指针缓存只存在内存中，不参与序列化。

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
- 选择配置 **「调试当前Playground文件」**（会先执行 Playground 构建任务）

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
