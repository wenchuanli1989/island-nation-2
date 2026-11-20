# Domino 游戏内核项目

基于 C23 的开放世界模拟内核，支持单机离线和在线多人两种模式。当前重点是本地引擎、公共数据结构、日志、导航、NPC AI 和 JSON 存档；在线／远程桥接层只有目录骨架，后续再接网络通信。

**版本**：0.0.1 | **C 标准**：C23 | **构建**：CMake + Clang

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [项目宪法](doc/项目宪法.md) | 核心约束：总则、生命周期、GameTimer、消息队列、日志、存档、简化校验、游戏时间、模块调用、业务数据、客户端边界 |
| [AGENTS.md](AGENTS.md) | 实现细则与现状：构建细节、生命周期调用顺序、数据架构、NPC AI、存档格式、编码规范 |
| [npc_ai.md](npc_ai.md) | NPC AI（动机 → 任务 → 行为 → 动画）设计边界 |
| [blueprint.md](blueprint.md) | 长期设计目标：核心玩法、宿主世界、账号与国家交互、模拟流水线阶段 |

## 模块结构

```
domino/
├── shared/              # 公共类型、错误码、日志、线程队列、读写锁及文件／时间／路径工具
├── engine/              # 本地引擎核心：common/host/human/nav/social/storage/time/game_timer/logger
├── engine-remote/       # 远程引擎桥接层，骨架
│   ├── local/           # 计划用于本地代理远程 engine
│   └── server/          # 计划用于在线模式服务端入口
├── client-hub/          # 客户端适配层，骨架
├── lib/                 # 第三方库：yyjson、klib、tinycthread
└── playground/          # 算法实验和子系统原型验证
```

## 当前状态

- **引擎生命周期：** 对外只允许 `Init → Start → Stop → Destroy`；只调用过 Init 的实例也须先 Stop 再 Destroy。销毁后可在同一进程重新 Init 并切换存档。接口返回 `void`，非法顺序或错误线程调用直接报错退出。详见宪法第一章和 AGENTS.md 的「生命周期实现」。
- **骨架模块：** client-hub、engine-remote/local 目前只管理各自的日志资源，提供四阶段入口（如 `dominoClientHubInit()`），尚未代管引擎。
- **客户端通道：** `dominoEnginePushMsg()`／`dominoEnginePopMsg()` 只有通道骨架，尚未定义任何业务场景，主循环会丢弃收到的消息。
- **NPC AI：** 第一阶段只实现恢复精力动机与自主睡眠。
- **存档：** JSON 格式，版本 31，不兼容旧格式。保存时先写 staging 目录再 rename 提交；同一存档目录不允许多个进程同时使用。样例存档位于 `playground/sandbox/domino`。

## 环境要求

- Linux，目标环境为 Ubuntu 26.04 LTS
- CMake 4.2.1+、Clang 22（C23）
- glibc 2.43+：时间采样依赖 `timespec_get` 的 `TIME_MONOTONIC`；系统 glibc 版本较低时，见下文「独立 glibc」
- Docker／Docker Compose（可选，用于远程开发）

## 构建

所有构建都通过 `scripts/build.sh` 执行，产物位于 `build-<preset>/bin/` 和 `build-<preset>/lib/`。

```bash
./scripts/build.sh debug                 # 调试模式
./scripts/build.sh debug engine          # 只构建单个目标：shared, engine, engine-remote-local, engine-remote-server, client-hub, playground
./scripts/build.sh release               # 发布模式（-O3, LTO）
./scripts/build.sh release-native        # 发布模式加 -march=native，产物仅限本机运行
./scripts/build.sh profile               # gprof 性能分析
./scripts/build.sh sanitize              # ASan + UBSan + LSan
./scripts/build.sh tsan                  # ThreadSanitizer
./scripts/build.sh clean                 # 清理所有 build-* 目录
```

### 独立 glibc

当系统 glibc 低于 2.43（例如 Ubuntu 24.04 的 2.39）时，通过环境变量 `DOMINO_GLIBC_ROOT` 指定独立安装的 glibc 2.43：

```bash
DOMINO_GLIBC_ROOT="$HOME/.local/opt/glibc-2.43" ./scripts/build.sh debug
env -u DOMINO_GLIBC_ROOT ./scripts/build.sh debug    # 改用系统 glibc
```

- **安装：** `scripts/setup-glibc.sh` 安装 Ubuntu 官方 `2.43-2ubuntu2.4` 软件包，并按 `scripts/glibc-2.43-amd64.sha256` 校验。它不替换系统 glibc，也不设置全局 `LD_LIBRARY_PATH`。
- **构建与运行：** 工具链会同时设置 sysroot、ELF 加载器和运行期库路径，生成的程序可以直接运行，但运行时必须保留该安装目录。
- **切换：** 改变或取消该变量时，构建脚本会自动清理对应产物并重建 CMake 缓存。

## Playground

```bash
./scripts/build.sh playground-run playground/算法/路径规划/弗洛伊德/main.c          # 构建并运行
./scripts/build.sh playground-build playground/算法/路径规划/弗洛伊德/main.c debug  # 只构建
```

默认 Playground 会完成一次引擎生命周期后退出，例如 `(cd build-debug && ./bin/domino-playground)`。存档写在 `build-debug/runtime/sandbox/`，再次运行会加载该存档。

### NPC AI 验证

```bash
./scripts/build.sh playground-run playground/npc_ai/main.c debug      # 也支持 release、sanitize、tsan
```

验证分两步：

1. 运行模块内部夹具 `engine/src/human/playground.c`，覆盖结算一致性、规划请求与结果复核、候选提交与容量、行为依赖、应激让位与插入，以及规划触发等。
2. 在真实线程下，用样例存档的副本运行端到端场景：自主睡眠、冻结、停止后读档继续、条件失效与恢复、仅 Init 的生命周期，以及睡眠被通话打断后恢复。

端到端存档写在构建目录的 `runtime/` 下。验收清单见 [npc_ai.md](npc_ai.md) 第 12.4 节。

## 代码质量

```bash
./scripts/build.sh format engine                 # clang-format，须指定模块或目录
./scripts/build.sh clang-tidy                    # 静态检查
./scripts/build.sh clang-tidy --strict engine    # 严格模式，发现错误时退出
./scripts/build.sh scan-build                    # Clang Static Analyzer
```

`format` 必须指定目标，不带目标时脚本直接报错，避免改写 `lib/` 下的第三方代码；只格式化本次改动的模块，例如 `format shared`、`format playground/npc_ai`。

## 开发环境

### macOS 本地调试（Intel Mac）

只支持默认 Playground 的 Debug 构建，且 SDK 须支持 `timespec_get(TIME_MONOTONIC)`；不满足时请改用 Linux 或 Docker。

```bash
./scripts/build.sh macos-debug
```

- **产物：** 位于 `build-macos-debug/`，与 Linux 的 `build-debug/` 分开。
- **线程库：** macOS 缺失的 `<threads.h>` 由 [TinyCThread](lib/tinycthread/README.md) 提供。
- **调试：** VS Code 中选择「调试Playground」按 `F5`，会先构建再调试固定入口 `playground/src/main.c`。

### Codex Cloud

`setup-codex-cloud.sh` 面向 Debian 13（trixie）x86_64，普通用户即可执行，需要联网。在 [Codex Cloud 环境设置](https://learn.chatgpt.com/docs/environments/cloud-environments)中配置：

| 配置 | 内容 |
| --- | --- |
| Install script | `bash setup-codex-cloud.sh` |
| Start instructions | 每个新 shell 先 `source /workspace/.local/domino/env.sh` |
| Environment variables（可选） | `DOMINO_CLOUD_ROOT=/workspace/.local/domino`（默认值；可改为其他可写绝对路径，不含空白、双引号或反斜杠） |
| Public internet access | 启用 Package managers，允许 `deb.debian.org`、`apt.llvm.org`、`archive.ubuntu.com` |

- **安装内容：** 隔离 APT 配置，校验 LLVM 密钥和软件源签名后，解包 Debian `trixie-backports` 的 CMake、Clang 22、调试分析工具和 Sanitizer 运行库，并通过 `scripts/setup-glibc.sh` 安装独立 glibc 2.43。
- **不改动系统：** 不调用 `sudo`，不修改系统软件源、密钥或 dpkg 数据库，也不执行软件包维护脚本。
- **使用前提：** 脚本须连同仓库使用，可重复执行以刷新安装；基础镜像须已有 APT、curl、GnuPG、dpkg-deb、GCC、Make、Git、Perl 和 Python 3.13。

```bash
bash setup-codex-cloud.sh
source /workspace/.local/domino/env.sh
./scripts/build.sh debug
(cd build-debug && ./bin/domino-playground)
```

2026-10-30 已在真实 Codex Cloud（Debian 13.6，无 sudo 的普通用户）完成验证：首次与重复安装，`debug`、`release`、`sanitize` 构建，以及 Playground 的保存和重载都通过。默认执行沙箱的 ptrace 限制会挡住 LeakSanitizer，需要放宽沙箱后再以同一普通用户运行。

### Ubuntu 26.04 服务器

```bash
sudo bash setup-dev.sh
sudo LLVM_VERSION=22 bash setup-dev.sh    # 指定 LLVM 主版本
```

- **使用方式：** 脚本可以单独复制到服务器运行，不需要 Docker 或整个仓库；只支持 Ubuntu 26.04，使用系统 glibc，通过 APT 安装 CMake、LLVM、调试和分析工具。
- **注意：** 脚本会把 root 的 Git 身份设为 `domino <domino@domino.com>`、创建 SSH 密钥，并开启 root 密码登录（密码 `123456`），只适合开发环境。

### Docker + VS Code Remote

镜像基于 Ubuntu 26.04 LTS，构建时直接调用 `setup-dev.sh`。

1. 启动：`docker compose up -d --build`（更新 Dockerfile 或安装脚本后重新执行）
2. VS Code 执行「Remote-SSH: Connect to Host」，连接 `root@localhost -p 10022`，密码 `123456`
3. 建议安装插件：clangd、CodeLLDB、clang-format、GitLens

连接后验证工具链：

```bash
command -v clang clangd clang-format clang-tidy lldb scan-build
./scripts/build.sh playground-build playground/src/main.c debug
```

### VS Code 任务与调试

- **Playground**：对当前 C 文件执行构建或构建并运行（默认）
- **Playground (Debug Build)**：只构建当前 Playground 文件
- **清理构建**、**运行clang-format**、**运行clang-tidy**
- **调试**：设置断点后按 `F5`，选择「调试Playground」，会先执行 Playground 构建任务

## 第三方库

| 库 | 位置 | 说明 |
| --- | --- | --- |
| yyjson | `lib/yyjson/` | JSON 解析 |
| klib | `lib/klib/` | 只保留 `kvec.h`、`khashl.h`；随仓维护版，基于官方 commit `97a0fcb7`，已带分配溢出检查、resize 失败返回值等本地补丁 |
| TinyCThread | `lib/tinycthread/` | 为 macOS 提供 `<threads.h>` |

第三方代码保持原样，不做修改；本地补丁的具体内容见 AGENTS.md 的「第三方库」。

## 待办

- CI/CD 配置
