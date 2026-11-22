# 待办

### NPC AI

- [ ] 评估阶段执行、延后结构变更：按阶段依赖和数据读写范围组织并行计算，将实体、任务、行为增删及容器布局变更记录为命令，在阶段同步点由执行器统一提交。普通字段访问仍须避免读写冲突，借用指针不得跨结构提交保留，并保持冻结、停止的有限批次语义。

### 本地开发

- 研究c语言调试和分析工具
- 研究-ftrapv是否在cmake中生效
- [ ] 升级 Clang 时检查 TSan 运行库（`libclang_rt.tsan`）是否已拦截 C11 线程函数（`thrd_*`／`mtx_*`／`cnd_*`）；支持后删除 `DOMINO_USE_TINYCTHREAD`，`tsan` 预设改回 glibc 的 `<threads.h>`，并同步 AGENTS.md、README 与 `lib/tinycthread/README.md`。

### 持续集成

- CI/CD 配置

### 网络通信

- 高频数据（位置更新）通过 P2P 传输（降低延迟）
- 关键数据（击杀、得分）通过服务器验证（保证安全）

#### 网络库选型

- GameNetworkingSockets 网络加密，P2P以及NAT穿透
- ENet
