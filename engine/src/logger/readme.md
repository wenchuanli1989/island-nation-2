# engine 日志接入

公共日志接口位于 `shared/include/domino_shared_logger.h`，实现位于 `shared/src/logger.c`。
engine 保留自身日志实例、子模块枚举、日志宏和初始化／启动／停止／销毁入口。引擎负责这些内部包装入口的调用顺序，包装层不维护额外生命周期状态或重复校验；shared 日志实例继续执行独立公共接口的状态校验。

shared logger 首次对象仍须零初始化，Destroy 负责清空，Init 不重复整体清零。默认日志等级仅在 Init 时解析到各模块，不保留冗余实例字段。`dominoLogVa()` 集中完成空指针、格式指针和等级过滤，模块查找辅助函数只匹配模块与等级；通过过滤的日志继续要求 STARTED，其他生命周期入口也保留原有状态检查。内部格式化、入队和 writer 使用已验证的参数与模块下标，分配、格式化、队列和文件操作失败检查不变。

- `dominoLoggerModuleInit()` 注册 core、storage、common、host、human、nav、social，分配队列并打开配置的文件，不启动 writer，返回 void。
- `dominoLoggerModuleStart()` 在引擎 Start 阶段、各业务线程启动之前创建 writer，返回 void。
- `dominoLoggerModuleStop()` 在全部日志生产者结束后拒绝新消息、排空日志并 join writer，再完成文件 fflush 和 fsync、stdout 刷新，保留队列和文件，返回 void。
- `dominoLoggerModuleDestroy()` 在 Stop 完成后关闭文件并释放队列等资源，不停止或 join writer，返回 void。
- 日志服务的致命错误直接调用 `common.c` 的 `dominoExitWithErrorAt()`，直接打印 stderr 并 `exit(EXIT_FAILURE)`，不进入日志队列、不等待日志排空或 join；不再接入 engine 首错状态或故障回调。
- 所有等级日志只写一个目标：`DominoLogModule.file_path` 指定的模块文件优先，其次 `DominoLogConfig.file_path` 指定的实例默认文件，两者都未配置时写 stdout。nullptr 或空路径视为未配置；非致命 ERROR 也遵循这一规则，不写 stderr。
- 队列满时生产者向当前模块的同一输出流同步提示一次并刷新，再等待空位直到入队；提示不再次入队。等待采用队列条件变量，writer 消费后唤醒生产者。
- 日志通过过滤和生命周期检查后，由业务线程在整理来源、正文和等待入队前逐条采样 UTC 时间，以毫秒格式随记录按值保存；writer 使用该调用时间戳，排队或延迟写出不改变它。UTC 墙上时间不用于计算累计未冻结业务时长。
- logger 统一以追加模式打开文件。Stop 在 writer 排空退出后，对各模块文件及实例默认文件先 fflush、再 fsync，正常返回保证文件日志已完成这两步同步，任一步失败直接致命退出；文件留到 Destroy 关闭。Stop 对继承的默认文件只同步一次，Destroy 只关闭一次；被使用的 stdout 仅 fflush、不关闭、不承诺 fsync。
- Stop 调用 `dominoThreadQueueBeginFlush()`，设置 `flushing_flag` 拒绝新消息并唤醒 writer；writer 消费完剩余日志，收到 `ERR_QUEUE_FLUSHING` 后退出。已 Start 时直接 join 并同步文件、刷新 stdout，不轮询线程完成状态；仅 Init 时没有 writer，仍关闭入队并完成相同的同步与刷新。每次 Init 后只调用一次 Stop，之后不再写日志或重新 Start；Destroy 不内部调用 Stop。
- 初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断统一在调用点使用 `DOMINO_PRINT_MESSAGE(fmt, ...)` 直接写 stdout，由 `common.c` 输出调用位置和完整正文、追加换行并刷新，不截断、不退出，也不调用日志记录接口。
- 运行阶段使用 `DOMINO_ENGINE_LOG`，所有等级（包括非致命 ERROR）交给实例异步处理。日志层只转发到实例，不维护阶段标志或选择 stdout。
- 导航、计时器等共用业务接口只返回可恢复结果，由掌握调用语义的边界记录一次：运行阶段使用日志宏，非运行阶段统一使用 `DOMINO_PRINT_MESSAGE`；若结果在该场景下属于致命故障，直接使用 `DOMINO_EXIT_WITH_ERROR`，写 stderr 并退出，不先记录日志。
- 开始、停止、冻结、解冻操作本身的消息直接打印；业务线程正在完成的批次仍使用运行日志。引擎 Stop 先 join 主循环、human 和 timer，完成人物结算，再调用 logger Stop，最后进入 `STOPPED`。引擎 Destroy 保存并释放世界后再调用 logger Destroy；保存和销毁期间的非致命诊断统一使用 `DOMINO_PRINT_MESSAGE` 直接写 stdout，不再进入日志队列。
- logger 在引擎 Init 时只准备资源，Start 时先启动 writer。引擎 Stop 同时接受 `INITIALIZED` 和 `RUNNING`：仅 Init 时没有任何线程需要 join，logger 关闭入队并同步文件、刷新 stdout，世界保持已加载事实，不结算人物；运行过的实例在业务线程退出及结算后停止 writer。两条路径都由外部先 Stop 再 Destroy；Destroy 只接受 `STOPPED`，不停止任何线程。
- writer 独立于引擎 Freeze/Thaw，冻结期间仍消费日志。

shared 对应提供 `dominoLogInit()`、`dominoLogStart()`、`dominoLogStop()` 和 `dominoLogDestroy()`。实例用共享四态区分未初始化、已初始化、已启动、已停止；Init 仅接受未初始化，Start 仅接受已初始化，Stop 仅接受已初始化或已启动，Destroy 仅接受已停止，重复或错序直接输出 stderr 并退出；有效日志只允许在 Start 后、Stop 前写入，其他阶段直接报致命错误，不会在无 writer 时入队阻塞。空实例、非法等级和被过滤的模块仍直接丢弃。client-hub 和 engine-remote 各自持有日志实例，由所属入口显式按 Init → Start → Stop → Destroy 调用；日志包装器直接使用实例状态，不再额外维护初始化指针或布尔标志。非运行阶段的非致命诊断在调用点统一使用 `DOMINO_PRINT_MESSAGE` 直接写 stdout。所有实例遵守同一满载等待和致命错误退出约定。
