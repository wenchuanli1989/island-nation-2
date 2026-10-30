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
- 启动线程顺序：logger → human 执行器 → planner → game_timer → 主循环；全部创建完成后进入 RUNNING，再解冻并唤醒业务线程。
- 停止时依次 join 主循环、human 执行器、planner、game_timer，统一结算后排空并 join logger。仅 Init 的实例由引擎跳过 human／game_timer Stop 和全部 join，仍调用 logger Stop 关闭入队并刷新。两条路径均保留实例资源供 Destroy 使用。
- time 的接入位置：引擎在 storage 加载前调用 `dominoTimeModuleInit()`，在保存与世界资源清理完成后调用 `dominoTimeModuleDestroy()`。
- Destroy 不调用 Stop 或 join；复位范围包括容器指针／数量／容量、索引、模块状态、线程句柄存储、引擎配置和路径。

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
