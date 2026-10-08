# AGENTS.md

本文件为 Codex (Codex.ai/code) 在本仓库中工作时提供指导。

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

引擎（`engine/src/`）包含以下子系统，每个子系统通过 `entry.c`/`entry.h` 暴露 `<name>ModuleInit()` 和 `<name>ModuleExit()` 接口：

- **host** — 宿主世界，AI 实体生活的模拟世界
- **human** — 人类个体模拟（游戏中唯一的决策单元）
- **nav** — 导航与寻路（Floyd-Warshall 算法实现）
- **social** — 社会系统（组织、家庭、国家）

引擎生命周期：`dominoEngineInit()` → `dominoEngineRun()` → `dominoEngineExit()`

### 模拟流水线设计目标

模拟逻辑计划按顺序组织为以下阶段：
1. **物理阶段** — 物体运动、空间分布、环境模拟（气候、地形、水文）
2. **个体生物智能阶段** — 人类、动物
3. **群体生物智能阶段** — 家庭、城市、国家、公司、帮派、宗教组织
4. **超级智能阶段** — 观察/学习/预测玩家行为，增强游戏体验

每个阶段对目标对象进行综合排序和过滤，多线程分批次处理，并产生事件消息供后续阶段使用。

### 数据架构

- 全局数据以扁平数组存储；哈希表（khashl）将实体 ID 映射到数组索引，实现 O(1) 检索
- 采用双缓冲分离多线程读写（以脏读换取读取性能）
- 宿主世界设计为只能运行在单一物理机器上（支持离线运行）
- 第三方库：**yyjson**（JSON 解析器，位于 `lib/yyjson/`）、**klib**（`khashl.h` 哈希表、`kvec.h` 动态数组，位于 `lib/klib/`）

### 时间语义

- 业务时间已确定使用 **32 位累计未冻结现实毫秒**：统一类型为 `domino_runtime_ms_t`（`uint32_t`），覆盖当前时刻、timer 的延迟与到期时间、任务／行为时长、NPC AI 处理节奏及收益结算。`dominoTimeGetRuntimeMs(&now_ms)` 返回状态码，成功时输出已校验范围的 32 位毫秒；换算只将现实纳秒除以 1000000，不应用游戏日历倍率。
- `0` 与 `UINT32_MAX` 均是合法业务毫秒值；累计范围约 49.71 个未冻结现实日，按当前日历规则约合 248.55 个游戏年，这是已接受的产品范围。加法前检查 `delta_ms <= UINT32_MAX - base_ms`，减法前检查先后关系，越界报错，不回绕、截断、饱和或归零。未经明确变更需求，不得把累计未冻结现实毫秒改成或建议改成 64 位，也不得把跨存档累计或这一范围当成必须扩宽的缺陷。
- time 内部单调采样和存档基准保留 `uint64_t` **纳秒**以保留精度；UTC 墙上时间戳与消息／计时器 ID 也可为 64 位。它们不是 64 位业务毫秒接口，不改变上述 32 位约定；业务代码不得用底层纳秒入口或 UTC 时间绕过毫秒范围检查。
- 游戏日期和日内时分的倍率仅用于显示层换算，不改变 time 的累计基准或业务计时。时间变量、参数和字段须带 `_ns`、`_ms`、`_s` 等单位后缀，禁止把显示用的游戏时间当作业务时长。
- 产品要求单次游戏会话最多持续 8 个现实小时，届时保存并退出，再次加载存档继续；这不是当前已经实现的自动退出能力。单次会话时长须独立于存档中的累计计时基准统计，加载存档继续累计未冻结现实时间，不因开始新会话将累计时钟归零。

### Playground

`playground/` 用于算法实验和子系统原型验证。每个实验独立存放于各自目录下，包含 `main.c`。构建系统可将 `PLAYGROUND_SOURCE_DIR` 指向 `playground/` 下的任意子目录。在 `DOMINO_PLAYGROUND_MODE` 模式下，各模块可通过 `playground.c` 直接测试内部实现逻辑。

## 编码规范

**语言规则：**
- C23 标准，使用 Clang 编译
- 多线程统一使用 C23 标准 API：`<threads.h>` 的 `thrd_*`、`mtx_*`、`cnd_*`、`tss_*`、`call_once`，原子操作使用 `<stdatomic.h>`。项目自有代码不直接使用 pthread 或操作系统线程 API，也不将标准同步对象强转为原生类型；第三方库和标准库底层实现不在此限制范围内。
- 使用固定宽度整数类型（`uint32_t`、`int16_t` 等）
- 跨平台：须支持 Windows、macOS、Linux、Android、iOS
- 使用 Doxygen 风格注释（`@brief`、`@param`、`@return`）

**命名规范：**
- 文件：`snake_case.c` / `snake_case.h`
- 函数：`camelCase`，动词开头（如 `createPlayer`、`navModuleInit`）
- 变量：`snake_case`，带语义后缀（`_count`、`_size`、`_len`、`_ptr`、`_str`、`_buf`、`_index`、`_flag`）
- 结构体/联合体：`PascalCase`（如 `DominoPlayer`）
- 宏/枚举值：`UPPER_SNAKE_CASE`
- 指针声明：星号靠左（`int* ptr`）
- 类型别名：`lowercase_t` 后缀（如 `domino_human_id_t`）

**错误处理：**
- 成功返回 0 或正值，失败返回负值
- 使用 `goto` 进行资源清理（Linux 内核风格）
- `assert()` 仅用于内部不变量检查，不可用于外部输入校验

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
- 暂时不考虑api跨平台问题，在linux平台编译通过就行
