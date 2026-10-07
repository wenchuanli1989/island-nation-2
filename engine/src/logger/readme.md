## 日志模块核心逻辑（实现参考 `engine/src/logger/log.c`）

### 总览
- **独立实例**：engine 公开 `DominoLogger` 的完整定义，调用方提供固定地址的实例存储；engine 在 `logger/entry.c` 中持有自己的实例，client-hub、engine-remote/local 和 engine-remote/server 各自持有另一个实例。
- **生命周期**：engine 保持 `dominoLoggerModuleInit()` / `dominoLoggerModuleExit()` 接口及原有日志宏；初始化失败时清理自己的实例并返回具体错误码，重复初始化返回 `ERR_ALREADY_INITIALIZED`。
- **异步日志**：业务线程只负责构造 `DominoLogQueueItem` 并入队；由后台 **writer 线程**批量取出并写入 `stdout/stderr`（以及可选文件）。
- **线程安全**：每个实例持有独立的 `DominoThreadQueue`，通过 queue 模块的阻塞生产、消费接口协调业务线程与 writer。
- **输出格式**：每行前缀包含时间戳、tid、seq、level、module、source location。
- **隔离范围**：队列、writer、文件句柄、模块表及 seq 均属于实例；独立文件输出需配置不同路径。stdout/stderr 仍是进程共享输出流，不同实例之间没有全局输出顺序。
- **线程标识**：tid 仍由进程级原子计数器和线程局部缓存分配，不是系统线程 ID；同一生产者线程写入不同实例时使用同一个 tid。

### 数据结构
- **队列元素 `DominoLogQueueItem`**
  - `id`：首字段 `uint64_t`，由 queue 模块在入队时写入，只作为队列条目标识，与日志输出的 `seq` 分离。
  - `source[]`：调用日志 API 时生成的有界 `file:line:func` 快照；队列不借用调用方的 `file/func` 指针。
  - `body[]`：正文缓冲（保证 `'\0'` 结尾）。
  - `body_len`：正文有效长度（**不包含**结尾 `'\0'`），供 writer 侧按长度拷贝/写出。
- **实例状态 `DominoLogger`**
  - 队列指针：`struct DominoThreadQueue* queue_ptr`，队列存储、读写位置及同步对象由 queue 模块管理。
  - 模块注册表：`modules[]` + `module_count`。
  - 序列号：`_Atomic uint32_t seq`，在本实例内通过 `atomic_fetch_add + 1` 分配 seq；并发生产者的入队及输出顺序不保证与 seq 顺序相同。

### 初始化与关闭
- **`dominoLogInit(config, modules_ptr, module_count, &logger)`**
  - `min_level` 只接受 0（默认 INFO）或 `TRACE..ERROR`，非法值返回 `ERR_INVALID_PARAM`，不会启动 writer。
  - 先校验并复制完整模块列表，再分配内部队列对象、调用 `dominoThreadQueueInit()` 并启动本实例 writer 线程；不保留调用方模块数组地址，返回后调用方可修改或释放数组。
  - `buffer_size` 为队列条目容量，0 使用默认值 10240；容量必须不超过 `UINT32_MAX`，且分配条目数组的大小计算不得溢出。队列初始容量与最大容量均设为该值，不扩容，满载时生产者阻塞等待空位。
  - 实例存储无需预先清零，失败时释放已分配的内部资源并复位对象，可在同一地址重试；模块列表不合法时不分配内部资源或启动 writer。
  - 初始化成功后 `DominoLogger` 须保持地址和存储生命周期，不能复制、移动或由调用方修改成员；已成功初始化且未销毁的对象不得重复初始化。
  - 队列同步对象在 `dominoThreadQueueInit()` 中创建，在 `dominoThreadQueueDestroy()` 中销毁，日志模块不再维护自己的队列锁和条件变量。
- **`dominoLogDestroy(logger_ptr)`**
  - 调用 `dominoThreadQueueStop(queue_ptr, DOMINO_THREAD_QUEUE_PRODUCER)` 停止生产，再调用 `dominoThreadQueueWakeAll()` 唤醒等待线程；此时不停止消费者，writer 继续排空已有日志。
  - writer 排空队列，对 `stdout/stderr` 及可选日志文件执行最终 `fflush` 后退出。
  - `thrd_join(writer_thread)` 等待 writer 线程退出后，调用 `dominoThreadQueueDestroy()` 并释放队列对象、关闭文件句柄及复位实例；不释放调用方提供的实例存储，同一地址可再次初始化。
  - 生命周期控制要求调用方先停止并等待本实例所有 producer；销毁不得与本实例其他 API 调用并发，其他实例仍可继续使用。局部实例须在离开作用域前销毁；销毁 nullptr、初始化失败后的对象或已销毁对象均无操作，未初始化的原始存储不可直接销毁。

### 入队与写出（生产者/消费者）
- **生产者 `dominoLogVa()` / `dominoLogMsgN()`**
  - 调用 `dominoThreadQueueProduce(queue_ptr, item_ptr, true)` 入队，队列满时阻塞等待空位；queue 模块负责锁、条件变量及 writer 唤醒。
- **消费者 `dominoLogWriterThread()`**
  - 每轮调用 `dominoThreadQueueConsumeBatch(queue_ptr, items, DOMINO_LOG_WRITER_BATCH_MAX, &batch_count, true)`，由 queue 模块在一次持锁操作中取出当前可用的最多 128 条日志。
  - 仅在队列为空时阻塞等待至少一条数据，有数据就立即返回当前批次，不等待凑满；环形回绕、FIFO 顺序及生产者空位通知均由 queue 模块处理。
  - 生产停止后，批量消费仍成功返回剩余日志；排空后的下一次调用返回 `ERR_QUEUE_PRODUCER_STOPPED`，writer 最终刷新并退出。
  - 队列同步 API 失败，以及运行期间生产或消费出现非预期错误时，日志模块向 stderr 报错并调用 `abort()`。

### 输出缓冲与关闭
- 运行期间，writer 使用 `fwrite` 写入输出流，底层写出时机由 stdio 的缓冲策略决定。
- 写日志 API 不等待输出流刷新；`dominoLogDestroy(logger_ptr)` 返回前会写完已接受的日志并完成最终刷新。

### 初始化模块列表
- **`DominoLogModule` 数组**
  - `module_id` 由调用方固定分配，engine 侧在 `logger/entry.c` 中将 `core/storage/common/host/human/nav/social` 的完整列表传入初始化。
  - `name` 必须非空且在 16 字节数组内以 NUL 结尾；`min_level` 只接受 0 或 `TRACE..ERROR`。
  - 模块 `min_level` 为 0 时继承实例默认等级；显式设置时只按模块等级过滤，可以低于或高于实例默认等级。
  - 初始化成功后模块表只读，不再提供独立注册接口；新增或修改模块须销毁实例后使用新列表重新初始化。
  - `module_id` 和 `name` 只需在同一实例内唯一，不同实例可复用。
  - 数量为 0 时允许数组指针为 nullptr；数量超过 `DOMINO_LOG_MAX_MODULES` 返回 `ERR_OUT_OF_RANGE`，非零数量配 nullptr 返回 `ERR_NULL_POINTER`。
  - 名称或等级不合法返回 `ERR_INVALID_PARAM`，重复 ID 或名称返回 `ERR_ALREADY_EXISTS`，整个初始化失败并清空对象，不保留部分模块表。

### 公共 API 调用

`dominoLogVa`、`dominoLog`、`dominoLogMsg` 和 `dominoLogMsgN` 的首个参数均为目标实例。engine 日志包装自动传入 engine 的实例，业务代码继续使用 `DOMINO_ENGINE_LOG` / `DOMINO_ENGINE_LOG_MSG`。其他模块使用各自的日志包装；多实例示例见 `engine/include/README.md`。

### 截断策略（现状）
- **入口校验**：配置和模块最小等级不合法时返回错误；写日志 API 收到非法等级或未注册模块时直接丢弃，不进入异步队列。
- **来源位置阶段**：调用线程最多复制 80 字节 file 和 32 字节 func，统一生成自包含 `source`；nullptr 规范化为 `?`。
- **正文构造阶段**：`vsnprintf`/拷贝时仅保证 `body` 在 `DOMINO_LOG_MESSAGE_MAX` 内并 `'\0'` 结尾；`body_len` 取实际可用长度。
- **最终输出阶段**：先检查 `snprintf` 返回值并钳制已写长度，始终为换行和 NUL 预留空间；source、前缀或正文截断时追加 `...(TRUNCATED)`。`fwrite` 长度不包含缓冲区结尾 NUL，每条输出最多 255 字节并以换行结束。
