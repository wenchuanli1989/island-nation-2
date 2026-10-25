# engine 公共头文件说明

`engine/include` 提供引擎生命周期和消息通信的公共 API。多实例日志与通用线程队列由 shared 提供；engine、client-hub 和 engine-remote 分别持有自己的日志实例。

## 文件职责

| 文件 | 说明 |
|------|------|
| `domino_engine.h` | 引擎 Init／Start／Stop／Destroy、时间冻结/解冻及消息收发接口。 |

## 引擎生命周期

退出请求直接由原子生命周期状态表示：`dominoEngineIsStopping()` 仅在 `STOPPING` 时返回 true，不维护独立布尔标志。`Start` 在创建线程前进入 `STARTING`；`Stop` 在停钟和唤醒前进入 `STOPPING`，完成业务线程 join、人物结算，再排空日志、join writer 并刷新输出后进入 `STOPPED`，所有资源保留。仅 Init、未 Start 时，所有线程尚未创建，引擎 Stop 跳过 human／timer Stop、全部 join 和人物结算，仍调用 logger Stop 关闭入队并刷新。随后由外部调用 `Destroy`，进入 `DESTROYING` 保存并释放运行资源、世界及日志，完成后回到 `UNINITIALIZED`。

首次 Init 使用静态初值，再次 Init 使用上一轮 Destroy 已复位的空状态；Init 只创建资源、绑定本轮配置和加载数据，不重复清空全局容器、世界计数或模块结构。time 无独立 Init 重置或提前采样，加载只恢复累计基准，Start 解冻时才建立单调起点。通用 queue／logger 首次使用的局部对象必须以 `= {0}` 初始化，动态对象须使用 `calloc` 或等价零初始化；静态对象自然满足。Init 先检查状态，再初始化资源；成功 Destroy 后可直接再次 Init。

Destroy 返回前，模块实例状态已恢复为空或初始值：common／host／social 的容器指针、数量、容量与索引指针清空，timer 整体复位，引擎线程句柄存储及配置清空。time 在存档和世界资源清理完成后执行 `dominoTimeModuleDestroy()`，由引擎保证在客户端线程的 `DESTROYING` 路径中调用，将累计基准、采样起点和快照版本归零并保持冻结；它只清理内存，不修改已保存时间，下一次 Init 仍从文件恢复。

生命周期和睡眠修改接口由 Init 成功登记的客户端线程串行调用。普通冻结、解冻及退出停钟均由客户端直接写入 time，不经过主循环转发，因此这些时间写入不会并发。正常顺序为 `Init（冻结）→ AuthorizeSleep／SubmitSleep → Start → Freeze／Thaw → Stop → Destroy`；仅 Init、未 Start 时仍须调用 Stop → Destroy。Stop 完成后不能直接 Start、Thaw 或继续业务修改；需要再次运行时须先 Destroy，再重新 Init。

引擎对外 Init／Start／Stop／Destroy 校验完整生命周期与调用线程：Init 只接受 `UNINITIALIZED`，Start 只接受 `INITIALIZED`，Stop 接受 `INITIALIZED` 或 `RUNNING` 并进入 `STOPPED`，Destroy 只接受 `STOPPED` 并恢复 `UNINITIALIZED`。重复调用或错误顺序均通过 `DOMINO_EXIT_WITH_ERROR` 输出 stderr 并退出。内部 timer、executor 和 planner 不另存完整生命周期状态，也不重复检查启动、停止或销毁顺序；引擎在运行过的 Stop 路径中先发布 `STOPPING` 并唤醒，再调用内部 Stop 进行 join。仅 Init 的路径跳过 human／timer Stop 和人物结算，仍调用 logger Stop 关闭入队并刷新；两条路径均可随后 Destroy 并再次 Init。

无线程世界、存储和唤醒关联模块按 Init → Destroy 使用，nav 按 InitBefore → InitAfter → DestroyBefore → DestroyAfter 使用，顺序由引擎保证。受引擎编排的内部辅助入口同样按资源有效的前置条件执行，不逐层复检已由调用链保证的模块、索引或固定队列初始化条件，也不为此增加状态或查询接口。公开道路增删等可直接调用的业务入口保留自身访问资格和输入检查，内部导航查询与路径辅助函数直接使用已建立的资源。shared logger／queue 是独立公共接口，继续检查各自的生命周期；引擎日志包装层不重复校验。资源分配、线程创建／join、锁、文件操作及业务参数检查仍保留。普通资源释放辅助函数、队列 BeginFlush／WakeAll 不属于四阶段生命周期入口。

| 接口 | 成功与失败边界 |
|------|------|
| `dominoEngineInit()` | 仅接受 `UNINITIALIZED`。准备日志文件、主循环及 human/timer 队列、运行映射表和同步对象，加载并校验世界；保持时间冻结，不创建线程。 |
| `dominoEngineStart()` | 仅接受 `INITIALIZED`。客户端使用 Init 已准备的资源，在冻结时钟下先启动 logger，再调用 human Start 完成启动校验并建立执行／规划线程，随后启动 timer，最后创建主循环。成功后进入 `RUNNING`，直接解冻时钟并唤醒主循环和业务线程；主循环不承担启动协调或发布启动回执。首次启动分批扫描人物建立调度，就绪不表示全部人物已经登记 timer、完成规划或启动任务；启动或解冻的致命错误直接打印 stderr 并退出进程。 |
| `dominoEngineFreezeTime()` | 客户端直接调用 time 冻结时钟，不向主循环发控制请求，不通知业务线程、不等回执。业务线程只在批次入口检查冻结，当前有限批次完整执行；原本等待中的线程保持等待。普通冻结不统一结算人物，也不保证可立即保存；冻结后的时刻范围校验失败直接打印 stderr 并退出进程。 |
| `dominoEngineThawTime()` | 客户端直接解冻时钟，再通知主循环、executor、planner 和 timer，不向主循环发控制请求，也不等待线程恢复回执。恢复续接留队消息和已有调度，不做全员扫描；时钟切换失败直接打印 stderr 并退出进程。 |
| `dominoEngineStop()` | 仅由客户端在 `INITIALIZED` 或 `RUNNING` 调用。仅 Init 时引擎跳过 human／timer Stop、全部 join 和人物结算，仍调用 logger Stop 关闭入队并刷新；运行过的实例先进入 `STOPPING`，冻结并校验时刻，再唤醒主循环和业务线程，暂停时无需解冻。依次 join 主循环、human 执行／规划线程和 timer，统一结算人物，再调用 logger Stop 排空日志、join writer 并刷新输出，完成后进入 `STOPPED`。世界、队列、同步对象和日志文件仍保留，全部线程已退出，尚未保存；所有投递者结束前保持目标队列有效。 |
| `dominoEngineDestroy()` | 仅接受 `STOPPED`，由客户端进入 `DESTROYING`，释放 timer 和主循环队列，准备导航并保存世界，再释放 human 运行资源和世界模块，最后关闭日志文件并释放日志队列；保存及销毁期间的非致命诊断通过 `DOMINO_PRINT_MESSAGE` 直接写 stdout。完成后回到未初始化状态，可在同一进程 Init 新世界。内部不调用任何 Stop，未先 Stop 就调用 Destroy 属于致命生命周期错误。 |

Init／Start／Stop／Destroy、Freeze／Thaw 返回 `void`，正常完成后返回，致命错误直接通过 `DOMINO_EXIT_WITH_ERROR` 输出 stderr 并调用 `exit(EXIT_FAILURE)`。初始化、开始、停止、冻结、解冻、销毁及运行阶段采用同一致命错误策略，不再提供实例首错查询或 `FAILED` 生命周期状态，不跨线程传递致命错误。标准退出处理仍可执行；调用方不能依靠致命错误后的正常返回执行重试、保存或清理。正常状态检查、启动就绪、退出状态、唤醒、线程 join 与资源释放顺序仍由生命周期协议保证。保存失败时，磁盘事务由后续新进程加载恢复。

Init 成功且尚未 Start 时可配置睡眠授权、提交待执行睡眠；运行后的暂停状态仍拒绝同步睡眠授权和提交，避免等待暂停中的业务消费者。`dominoEnginePushMsg()`／`dominoEnginePopMsg()` 和底层消息队列操作不受冻结影响；消息可留队，阻塞取消息仍可能等到业务线程恢复。日志也持续工作。业务线程每轮先检查退出、再检查冻结，暂停等待可由解冻或退出通知解除；当前批次的状态修改、规划请求、规划回传和 timer 登记完整执行，下一轮才等待。

睡眠授权、任务提交、查询和消息收发继续返回其业务或控制结果；队列空、正常排空结束、没有可行规划等不自动成为致命错误。运行阶段的非致命错误及正常日志通过日志模块输出，初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断统一使用 `DOMINO_PRINT_MESSAGE` 直接输出 stdout。错误日志本身不会退出，只有显式调用公共退出方法才终止进程。

日志初始化、启动、停止和销毁返回 `void`；日志队列满时向所属模块的输出流同步提示一次并等待直到入队，日志服务自身的致命错误直接打印 stderr 并 exit。业务队列使用非阻塞 Produce，达到最大容量立即返回 `ERR_QUEUE_FULL`，由业务边界决定拒绝请求或因必要消息无法交付而直接退出。第三方库保留既有终止行为。

## 日志接入

公共 API 及多实例示例见 `shared/include/README.md`，engine 的日志封装见 `engine/src/logger/readme.md`。
engine 按 Init → Start → Stop → Destroy 管理自己的日志实例；shared 日志实现不依赖 engine 生命周期，也不再维护故障回调或首错查询。
