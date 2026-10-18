## 日志模块核心逻辑（实现参考 `engine/src/logger/log.c`）

### 总览
- **独立实例**：engine 公开 `DominoLogger` 的完整定义，调用方提供固定地址的实例存储；engine 在 `logger/entry.c` 中持有自己的实例，client-hub、engine-remote/local 和 engine-remote/server 各自持有另一个实例。
- **生命周期**：engine 保持 `dominoLoggerModuleInit()` / `dominoLoggerModuleExit()` 及原有日志宏；初始化和退出均返回错误码。失败时立即返回并保留剩余资源，不清理、不回滚、不重试；模块适配器记住失败码，后续初始化和退出均返回该错误。重复初始化返回 `ERR_ALREADY_INITIALIZED`，同样保存为生命周期首错。
- **引擎错误诊断**：内部 `dominoEngineLog()` / `dominoEngineLogMsg()` 的 ERROR 同步写入 stderr，不依赖 logger 初始化、队列或 writer，也不上报诊断自身的格式化/输出故障，防止抢先覆盖调用方正在传播的业务首错。正文使用 512 字节栈缓冲，正文或来源位置截断均带 `...(TRUNCATED)`。
- **异步日志**：公共 `dominoLog*()` 所有等级及引擎包装的其他等级，由业务线程构造 `DominoLogQueueItem` 并入队；由后台 **writer 线程**批量取出并写入 `stdout/stderr`（以及可选文件）。
- **暂停与恢复**：日志线程不受引擎 Freeze／Thaw 影响，不检查业务暂停或冻结状态；暂停期间仍正常接收、消费和写出日志，无日志时等待新消息。日志队列满仍按统一规则上报首错并终结本次引擎实例。
- **线程安全**：每个实例持有独立的 `DominoThreadQueue`，生产者不等待队列空位，writer 可在无数据时阻塞消费。
- **输出格式**：每行前缀包含时间戳、tid、seq、level、module、source location。
- **隔离范围**：队列、writer、文件句柄、模块表及 seq 均属于实例；独立文件输出需配置不同路径。stdout/stderr 仍是进程共享输出流，不同实例之间没有全局输出顺序。
- **线程标识**：tid 仍由进程级原子计数器和线程局部缓存分配，不是系统线程 ID；同一生产者线程写入不同实例时使用同一个 tid。

### 数据结构
- **队列元素 `DominoLogQueueItem`**
  - `id`：首字段 `uint64_t`，由 queue 模块在入队时写入，只作为队列条目标识，与日志输出的 `seq` 分离。
  - `module_index`：生产者按模块 ID 和日志等级过滤后保存的模块表下标；writer 直接取得模块名称，无需再次查找。
  - `source[]`：调用日志 API 时生成的有界 `file:line:func` 快照；队列不借用调用方的 `file/func` 指针。
  - `body[]`：正文缓冲（保证 `'\0'` 结尾）。
  - `body_len`：正文有效长度（**不包含**结尾 `'\0'`），供 writer 侧按长度拷贝/写出。
- **实例状态 `DominoLogger`**
  - 队列指针：`struct DominoThreadQueue* queue_ptr`，队列存储、读写位置及同步对象由 queue 模块管理。
  - 模块注册表：`modules[]` + `module_count`。
  - 序列号：`_Atomic uint32_t seq`，在本实例内通过 `atomic_fetch_add + 1` 分配 seq；并发生产者的入队及输出顺序不保证与 seq 顺序相同。
  - 生命周期结果：原子 `lifecycle_result` 保存初始化、运行或退出的首个错误，`writer_completed_flag` 表示 writer 已完成工作。日志入队和 writer 消费失败还通过 `dominoEngineReportFailure()` 上报引擎，终结本次引擎实例；日志实例及未回收资源须保活到宿主结束进程。

### 初始化与关闭
- **`dominoLogInit(config, modules_ptr, module_count, &logger)`**
  - `min_level` 只接受 0（默认 INFO）或 `TRACE..ERROR`，非法值返回 `ERR_INVALID_PARAM`，不会启动 writer。
  - 先校验并复制完整模块列表，再分配内部队列对象、调用 `dominoThreadQueueInit()` 并启动本实例 writer 线程；不保留调用方模块数组地址，返回后调用方可修改或释放数组。
  - `buffer_size` 为队列条目容量，0 使用默认值 10240；容量必须不超过 `UINT32_MAX`，且分配条目数组的大小计算不得溢出。队列初始容量与最大容量均设为该值，不扩容，满载立即返回 `ERR_QUEUE_FULL`，由日志模块保存首错并上报引擎。
  - 实例存储无需预先清零，初始化失败直接返回并保留已获取资源，不允许重试；模块列表不合法时不分配内部资源或启动 writer。
  - 初始化开始后 `DominoLogger` 须保持地址和存储生命周期，不能复制、移动或由调用方修改成员，直至成功销毁；生命周期失败后由宿主决定何时结束进程，此前须保持实例和残留线程资源有效。调用方不得对尚未成功销毁的对象再次初始化。
  - 队列同步对象在 `dominoThreadQueueInit()` 中创建，在 `dominoThreadQueueDestroy()` 中销毁，日志模块不再维护自己的队列锁和条件变量。
- **`dominoLogDestroy(logger_ptr)`**
  - 调用 `dominoThreadQueueStopAndWake(queue_ptr, DOMINO_THREAD_QUEUE_PRODUCER)` 停止生产并唤醒等待线程；此时不停止消费者，writer 继续排空已有日志。
  - 停止唤醒成功后，退出调用线程通过原子结果和完成回执等待 writer；已发布错误立即返回，避免在失败后继续等待或 join。完成后调用 `thrd_join(writer_thread)` 并检查线程返回码，再依次刷新 `stdout`、`stderr` 及可选日志文件，关闭文件、调用 `dominoThreadQueueDestroy()` 并释放队列对象、复位实例。
  - 停止唤醒、writer 排空、join、刷新、关闭或队列销毁任一步骤失败时，保存首错并立即返回，停止后续清理；后续销毁仅返回已保存错误。线程等待、join 和队列同步失败分别保留具体错误码，刷新失败返回 `ERR_FILE_FLUSH`，写入失败返回 `ERR_FILE_WRITE`，关闭文件失败返回 `ERR_FILE_CLOSE`。
  - 生命周期控制要求调用方先停止并等待本实例所有 producer；销毁不得与本实例其他 API 调用并发，其他实例仍可继续使用。不释放调用方提供的实例存储，成功后同一地址可再次初始化。局部实例须在离开作用域前成功销毁；销毁 nullptr、零初始化对象或已成功销毁对象均返回 `CODE_OK`，初始化失败对象返回原始错误，未初始化的原始存储不可直接销毁。

### 入队与写出（生产者/消费者）
- **生产者 `dominoLogVa()` / `dominoLogMsgN()`**
  - 校验实例、日志等级和模块 ID，复用过滤时找到的模块下标；模块表在初始化后保持只读，直到 writer 退出后才清空。
  - 调用 `dominoThreadQueueProduce(queue_ptr, item_ptr)` 入队，不等待消费者腾出空位；queue 模块负责锁及 writer 唤醒。
  - 入队失败保存实例首错并直接调用 `dominoEngineReportFailure()`，随后双端关停队列并唤醒 writer；故障直接写入 stderr，不重新进入异步日志队列、不重试、不主动结束进程或回收资源。
  - 失败后的日志调用直接返回。失败不依赖 engine 已完成初始化，独立日志实例同样保留自身首错并上报；writer 观察失败后发布完成回执，宿主保活资源并结束进程。
- **消费者 `dominoLogWriterThread()`**
  - 每轮调用 `dominoThreadQueueConsumeBatch(queue_ptr, items, DOMINO_LOG_WRITER_BATCH_MAX, &batch_count, true)`，由 queue 模块在一次持锁操作中取出当前可用的最多 128 条日志。
  - 仅在队列为空时阻塞等待至少一条数据，有数据就立即返回当前批次，不等待凑满；环形回绕及 FIFO 顺序由 queue 模块处理，出队无需通知生产者。
  - 生产停止后，批量消费仍成功返回剩余日志；排空后的下一次调用返回 `ERR_QUEUE_PRODUCER_STOPPED`，writer 退出，最终刷新由销毁调用线程完成。
  - 消费返回错误时保存并上报首错，停止本实例队列，发布 writer 完成回执；生产路径的失败唤醒使用双端关停，不能只 `WakeAll` 后让空队列的阻塞消费继续等待。
  - UTC 时间戳采样或格式化失败，以及运行和关闭排空过程中的 `fwrite` 短写，均直接记录、保存并上报首错，不再继续写出本批剩余日志。队列内部同步原语的现有错误策略由 queue 模块维护。

### 输出缓冲与关闭
- 运行期间，writer 使用 `fwrite` 写入输出流，底层写出时机由 stdio 的缓冲策略决定；任何阶段发生短写后立即停止写出并返回错误，不重试写入。
- 写日志 API 不等待输出流刷新；`dominoLogDestroy(logger_ptr)` 成功返回前会排空关闭时剩余的日志队列并完成最终刷新。已失败的写入不会重试补写；失败返回不保证剩余日志已全部写出或资源已全部释放。

### 初始化模块列表
- **`DominoLogModule` 数组**
  - `module_id` 由调用方固定分配，engine 侧在 `logger/entry.c` 中将 `core/storage/common/host/human/nav/social` 的完整列表传入初始化。
  - `name` 必须非空且在 16 字节数组内以 NUL 结尾；`min_level` 只接受 0 或 `TRACE..ERROR`。
  - 模块 `min_level` 为 0 时继承实例默认等级；显式设置时只按模块等级过滤，可以低于或高于实例默认等级。
  - 初始化成功后模块表只读，不再提供独立注册接口；新增或修改模块须销毁实例后使用新列表重新初始化。
  - `module_id` 和 `name` 只需在同一实例内唯一，不同实例可复用。
  - 数量为 0 时允许数组指针为 nullptr；数量超过 `DOMINO_LOG_MAX_MODULES` 返回 `ERR_CAPACITY_EXCEEDED`，非零数量配 nullptr 返回 `ERR_NULL_POINTER`。
  - 名称或等级不合法返回 `ERR_INVALID_PARAM`，重复 ID 或名称返回 `ERR_ALREADY_EXISTS`；完整校验通过后才复制模块表，失败时保存错误码。

### 公共 API 调用

`dominoLogVa`、`dominoLog`、`dominoLogMsg` 和 `dominoLogMsgN` 的首个参数均为目标实例。engine 日志包装对 ERROR 同步直写 stderr，其他等级自动传入 engine 的实例；业务代码继续使用 `DOMINO_ENGINE_LOG` / `DOMINO_ENGINE_LOG_MSG`。公共 `dominoLog*()` 的异步实例契约保持不变。其他模块使用各自的日志包装；多实例示例见 `engine/include/README.md`。

### 截断策略（现状）
- **入口校验**：配置和模块最小等级不合法时返回错误；写日志 API 收到非法等级或未注册模块时直接丢弃，不进入异步队列。
- **来源位置阶段**：调用线程最多复制 80 字节 file 和 32 字节 func，统一生成自包含 `source`；nullptr 规范化为 `?`。
- **格式化失败**：来源位置、前缀或正文格式化返回负值时，直接向 stderr 记录 `ERR_STRING_FORMAT` 并上报首错，终结日志实例；正常空正文和长度截断仍按原有规则处理。
- **正文构造阶段**：`vsnprintf`/拷贝时仅保证 `body` 在 `DOMINO_LOG_MESSAGE_MAX` 内并 `'\0'` 结尾；`body_len` 取实际可用长度。
- **最终输出阶段**：先检查 `snprintf` 返回值并钳制已写长度，始终为换行和 NUL 预留空间；source、前缀或正文截断时追加 `...(TRUNCATED)`。`fwrite` 长度不包含缓冲区结尾 NUL，每条输出最多 255 字节并以换行结束。
