# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在本仓库中工作时提供指导。

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

引擎（`engine/src/`）包含以下子系统，各子系统通过 `entry.c`/`entry.h` 管理 Init／Destroy；logger、game_timer、human 等线程模块另提供 Start／Stop：

- **host** — 宿主世界，AI 实体生活的模拟世界
- **human** — 人类个体模拟（游戏中唯一的决策单元）
- **nav** — 导航与寻路（Floyd-Warshall 算法实现）
- **social** — 社会系统（组织、家庭、国家）

引擎生命周期：`dominoEngineInit()` → `dominoEngineStart()` → `dominoEngineStop()` → `dominoEngineDestroy()`；仅 Init、未 Start 时也必须先 Stop 再 Destroy。

项目自有模块的生命周期方法统一使用 Init／Start／Stop／Destroy，不保留旧生命周期名称或兼容别名。按实际职责提供阶段：任务存储、唤醒关联等无线程子模块只需 Init／Destroy，不增加空的 Start／Stop。进程致命退出、业务执行和单项资源释放函数按自身语义命名。

引擎对外生命周期入口串行且严格校验顺序及调用线程：Init 仅接受未初始化，Start 仅接受已初始化，Stop 接受已初始化或运行中，Destroy 仅接受已停止。重复调用、未初始化操作、未 Stop 就 Destroy、Stop 后直接 Start 均通过 `DOMINO_EXIT_WITH_ERROR` 输出 stderr 并退出。受引擎编排的内部模块和辅助调用链遵循资源有效的契约，不重复检查已由引擎保证的模块、索引或固定队列初始化条件，不维护仅为这些重复校验存在的状态或查询接口；无线程模块只需 Init／Destroy。可直接调用的公共业务入口仍负责访问资格及输入检查。业务参数、实体与任务事实、版本、容量及时间范围校验，以及线程就绪、业务完成、队列排空和真实资源失败检查仍保留。shared logger／queue 是独立公共接口，继续保留各自的生命周期边界校验；引擎日志包装层不额外维护状态或重复检查。 shared 的公共调用路径集中校验参数与实例状态，内部辅助函数依赖已建立的前置条件，不重复检查；条件等待会释放锁，唤醒后的排空状态、容量和唤醒版本等动态条件仍须在锁内重新检查。

- **Init**：准备日志文件、固定队列、运行映射表和同步对象，初始化世界并加载存档；时钟保持冻结，不创建线程。首次实例由静态初值建立空状态，再次实例由上一轮 Destroy 复位；Init 不重复清空全局容器、世界计数或模块结构。time 无独立 Init 重置或提前采样，加载只恢复累计基准，Start 解冻时建立单调起点。业务容器容量和保存时的临时缓冲仍按实际数据量分配。通用 queue／logger 的首次对象必须使用静态初值、`={0}` 或 `calloc`，使 Init 能在创建资源前可靠检查重复调用；Destroy 已清零的对象可直接再次 Init。
- **Start**：使用 Init 已准备的资源，依次启动 logger、human 执行器、planner、game_timer 和主循环；全部创建完成后进入 RUNNING，解冻并唤醒业务线程。
- **Stop**：运行过的实例冻结时钟并通知退出，依次 join 主循环、human 执行／规划线程和 game_timer，统一结算，再排空并 join logger；仅 Init 的实例由引擎跳过 human／game_timer Stop、所有 join 和人物结算，仍调用 logger Stop 关闭入队并刷新，然后进入引擎 STOPPED 状态。两条路径均保留资源供 Destroy 使用。
- **Destroy**：整理并持久化世界，释放运行资源、世界容器及日志资源，并立即将容器指针／数量／容量、索引指针和模块实例状态恢复为空或初始值，不依赖下一次 Init 覆盖旧状态。time 在保存后将内存基准、采样起点和快照版本归零并保持冻结；存档累计时间不变，下一次 Init 从文件恢复。引擎线程句柄存储、配置及路径同时复位，最后回到未初始化状态；不调用 Stop 或 join。允许成功 Destroy 后在同一进程重新 Init，不支持 Stop 后直接 Start。

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

- 业务时间已确定使用 **32 位累计未冻结现实毫秒**：统一类型为 `domino_runtime_ms_t`（`uint32_t`），覆盖当前时刻、game_timer 的延迟与到期时间、任务／行为时长、NPC AI 处理节奏及收益结算。`dominoTimeGetRuntimeMs(&now_ms)` 返回 `void`，正常完成时输出已校验范围的 32 位毫秒；空指针、采样失败、时钟倒退或当前时刻越界直接打印 stderr 并 exit。换算只将现实纳秒除以 1000000，不应用游戏日历倍率。
- `0` 与 `UINT32_MAX` 均是合法业务毫秒值；累计范围约 49.71 个未冻结现实日，按当前日历规则约合 248.55 个游戏年，这是已接受的产品范围。加法前检查 `delta_ms <= UINT32_MAX - base_ms`，减法前检查先后关系，越界报错，不回绕、截断、饱和或归零。未经明确变更需求，不得把累计未冻结现实毫秒改成或建议改成 64 位，也不得把跨存档累计或这一范围当成必须扩宽的缺陷。
- time 内部单调采样和存档基准保留 `uint64_t` **纳秒**以保留精度；UTC 墙上时间戳与消息／计时器 ID 也可为 64 位。它们不是 64 位业务毫秒接口，不改变上述 32 位约定；业务代码不得用底层纳秒入口或 UTC 时间绕过毫秒范围检查。
- 游戏日期和日内时分的倍率仅用于显示层换算，不改变 time 的累计基准或业务计时。时间变量、参数和字段须带 `_ns`、`_ms`、`_s` 等单位后缀，禁止把显示用的游戏时间当作业务时长。
- 产品要求单次游戏会话最多持续 2 个游戏年对应的现实时间，即 24 分钟 × 12 × 2 = 576 个现实分钟（9.6 小时），届时保存并退出，再次加载存档继续；这不是当前已经实现的自动退出能力。单次会话时长包含玩家冻结期间，须独立于存档中的累计计时基准统计，加载存档继续累计未冻结现实时间，不因开始新会话将累计时钟归零。

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
- 业务与控制接口成功返回 0 或正值，拒绝请求、查询未命中、队列为空或正常排空结束等继续返回约定结果；不能仅凭负结果码判断致命错误。初始化、开始、停止、冻结、解冻和销毁接口没有可恢复业务结果时返回 `void`。
- 所有阶段的致命错误通过 `DOMINO_EXIT_WITH_ERROR` 直接输出 stderr，由 `common.c` 中的公共方法执行 `exit(EXIT_FAILURE)`，允许标准退出处理。致命出口不进入日志队列、不等待日志排空或 join。不维护首错状态或跨线程致命错误传播；正常启动就绪、业务完成结果、退出通知与 join 同步仍须保留。
- 运行阶段的非致命错误和正常日志走日志模块，按模块文件、实例默认文件、stdout 的顺序选择已配置的输出目标；初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断统一使用 `DOMINO_PRINT_MESSAGE` 直接输出 stdout。 阶段按操作所属路径区分：停止操作自身直接输出，业务线程最后一批操作仍使用运行日志；先 join 全部业务线程，再排空并停止 logger，最后销毁资源。由掌握业务语义的边界记录一次，底层通用工具保留结果，不逐层重复打印；错误日志级别本身不触发退出。
- 正常销毁及可恢复操作按所有权清理资源，可使用 `goto` 集中处理；致命错误直接退出，不另建失败回滚、重试或清理传播链。
- `assert()` 仅用于内部不变量检查，不可用于外部输入校验；必须在正式运行中处理的致命错误使用显式检查和公共退出方法。
- **实际内存分配失败及分配大小溢出直接报错并退出进程。** 保留必要的空指针、返回值及大小检查，不为避免后续分配失败提前扩容、占位，也不增加重试、降级或复杂回滚。
- 公共通用工具保留头文件约定的输入校验、输出状态和返回结果，不因当前调用方已校验而删除独立调用边界。内部辅助函数复用调用方已确认且仍有效的条件，不重复检查、清零或维护可直接推导的计数；成功调用会完整写入的局部输出无需预清零。实际 IO、分配和格式化失败仍须检查，文件状态在磁盘操作后按需重新读取。
- 优先使用容器公开接口，不直接操作内部字段绕过接口。`lib/` 下第三方库代码保持原样，包括 `kvec` 原有分配失败终止行为及第三方断言；项目自有模块统一使用公共致命退出方法。业务容量、参数、格式、IO 错误及零长度请求根据调用语义保留业务结果或明确判定为致命错误，不用空指针未定义行为代替错误处理。

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
