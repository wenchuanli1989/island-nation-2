# engine 线程消息

`DominoThreadQueue` 由 shared 提供 FIFO 存储、控制唤醒和版本变化等待；通用接口见 `shared/include/domino_shared_queue.h`。
本目录定义 engine 业务消息 `DominoThreadMessage`，具体载荷类型由 `human/message.h` 和 `timer/message.h` 提供。
使用消息的代码须自行包含 `thread_message.h`；底层 shared 队列不依赖这些业务类型。
执行器、规划器等线程各自拥有一个收件队列，每条消息的 `type` 决定 `payload` 联合体的有效成员。
队列不按业务类型拆分，也不执行业务分派。

| 字段 | 职责 |
| --- | --- |
| `id` | 必须位于首字段，由收件队列回写；与载荷里的请求 ID、timer ID 独立 |
| `payload.timer` | 按值保存 `DominoTimer` 到期事件 |
| `payload.plan` | 按值保存 AI 请求／结果共用的 `DominoHumanPlanEnvelope` |
| `payload.command` | 按值保存 `HumanExecutionCommand` |
| `type` | TIMER、AI_REQUEST、AI_RESULT、COMMAND 等类型，决定读取哪个联合体成员 |

`queue.items_ptr` 是环形数组本身，存放完整的 `DominoThreadMessage`，包括内嵌载荷。
底层队列仍允许模块内部按固定记录传值，例如 timer 自有的登记命令队列。
等待和唤醒直接使用收件队列已有的互斥锁与条件变量；无需额外对象或绑定关系。队列不解释模块的业务状态。

## 消息发送与接收

收件队列以 `sizeof(DominoThreadMessage)` 初始化。发送者直接用结构体初始化对应联合体成员，
Produce 按值复制整条消息，无需为外层载荷逐条分配或释放内存。每个队列槽位的大小由最大载荷决定。
当前 x86_64 Clang 静态布局中，timer 为 32 字节、命令为 24 字节、AI envelope 为 128 字节，完整消息为 144 字节；
原描述符为 32 字节且另有堆载荷。新布局减少逐条分配和指针访问，同时增加每个槽位占用及队列复制字节数，尚无性能实测。

```c
DominoThreadMessage message = {
    .type = DOMINO_THREAD_MESSAGE_COMMAND,
    .payload.command = command,
};
DOMINO_CODE code = dominoThreadQueueProduce(&worker_inbox, &message, false);
if (code != CODE_OK) {
    // 由所属模块按业务语义处理投递失败。
}
```

接收者 Consume 得到完整消息，按 `type` 读取对应联合体成员；类型和载荷的对应关系由生产入口保证。
TIMER 和 COMMAND 没有独占的动态载荷，处理或丢弃后无需释放。timer 的队列指针是借用引用，业务方负责保持队列有效。
AI 的 envelope 同样内嵌，但 DRAFT 的行为数组仍为另行分配的独占块；按值复制不会递归复制该数组。
入队成功才移交行为块所有权，发送者之后不得读取、修改、释放或再次发送该行为指针；入队失败仍归发送者。
接收者处理或丢弃 AI 消息后调用 `dominoHumanPlannerReleaseMessage()`，释放未被任务系统接管的行为块并清空消息。
不能把栈地址或可扩容数组内部地址作为载荷中的借用指针交给异步消费者。

执行器收件队列可依次出现 `AI_RESULT → TIMER → COMMAND`；规划器当前只接收由 Submit 构造的 AI_REQUEST。
消息保留 FIFO；抢占、紧急提升和任务选择由接收模块处理。
暂停、恢复和退出状态由所属模块维护，使用原子变量或模块自己的锁同步；普通冻结和恢复不要求业务线程回执。

业务消息使用 `dominoThreadQueueProduce(..., false)`，不等待空位：队列满时按需扩容，达到最大容量后立即返回 `ERR_QUEUE_FULL`。日志满载后使用 `dominoThreadQueueProduce(..., true)` 等待空位并入队。
非阻塞仅指不等待消费者腾出容量，入队仍需取得队列互斥锁。队列的容量、参数及排空状态等结果仍通过返回值报告；分配和同步原语故障直接打印 stderr 并 exit。
所属模块根据业务语义处理返回值：可以拒绝尚未接受的请求，必要运行消息无法交付时直接致命退出，不维护首错或等待故障传播。

## 初始化和唤醒

引擎在 Init 阶段初始化全部固定收件队列和同步资源；引擎及各模块 Start 使用这些已有队列启动线程。仅 Init、未 Start 时没有线程需要 join，仍由引擎串行执行 Stop → Destroy。只需阻塞接收的模块可继续使用 `Consume(..., true)`；
需要响应控制状态的模块使用 `Snapshot → 检查状态和消息 → Wait`。
队列自身只有 Init → Destroy 两个生命周期入口。首次局部对象须以 `DominoThreadQueue queue = {0};` 初始化，动态对象须使用 `calloc` 或等价零初始化，静态对象自然满足；成功 Destroy 后可直接再次 Init。Init 拒绝已初始化对象，Destroy 拒绝空指针、未初始化对象和重复销毁。两者返回 `void`，阶段、参数、分配或同步错误通过 `DOMINO_EXIT_WITH_ERROR` 直接打印 stderr 并 exit，不向调用方逐层传播。

公共入口通过 `initialized` 明确判断实例是否已初始化；Init 完整建立资源后置为 true，Destroy 清空整个对象时复位为 false。`item_size` 只表示元素字节数，`items_ptr` 会在扩容中改变，均不承担初始化状态判断。Init 基于调用方或上轮 Destroy 提供的空对象建立资源，不重复整体清零。Produce 的两种模式共享一次参数及初始化检查，Consume 委托 ConsumeBatch；内部等待、扩容和复制步骤依赖已建立的前置条件。等待期间锁会释放，唤醒后的排空状态、元素数、容量及版本检查仍必须保留。

- 每次成功 `Produce` 都在队列锁内推进本队列的唤醒版本并通知等待者，包含 timer 投递 TIMER 消息的路径。
- 入队失败和出队不推进版本；出队后若存在等待空位的生产者，广播 `not_full`。
- `WakeAll` 推进本队列版本，并通知全部队列等待者，不占用任何队列容量。
- `BeginFlush` 在同一队列锁内设置 `flushing_flag`、推进版本并通知全部等待者；进入后拒绝生产，允许消费已有消息。
- 需要唤醒时，模块先发布新的控制状态，再调用 `dominoThreadQueueWakeAll()`。普通冻结只修改时钟状态，不通知等待者；解冻和退出须通知。
- 每个队列只唤醒自身等待者。成功入队时如有已登记的版本等待者或阻塞消费者，统一广播 `not_empty`，让它们重新检查条件；
  `waiter_count` 登记全部条件等待，`producer_waiter_count` 额外统计等待空位的生产者。多个消费者被唤醒后仍通过队列锁竞争消息。

timer 到期时发送 `type = DOMINO_THREAD_MESSAGE_TIMER`、`payload.timer` 内嵌 `DominoTimer` 的消息，与 AI、命令共用收件队列。
timer 内部用于取消、堆迁移和计时线程等待的同步协议不变。

## 消费者循环

每轮严格按以下顺序执行：

1. 用 `dominoThreadQueueSnapshot()` 记录本轮 `wake_epoch`。
2. 先检查模块退出状态，再检查时钟冻结状态；退出则结束循环，冻结则跳过普通业务处理并等待通知，不发送暂停回执。
3. 可以运行时，非阻塞地按有限批次消费收件队列，按消息类型分派，并处理本地就绪工作；中途冻结或普通退出不打断当前批次，下一轮再检查。
4. 仍有立即可做的工作则进入下一轮；否则调用 `dominoThreadQueueWait(queue_ptr, epoch)`，传入队列和第 1 步的版本值。
5. 版本变化返回 `CODE_OK` 时，从第 1 步重新检查；返回 `ERR_QUEUE_FLUSHING` 时进入模块退出或排空流程。

不能在检查队列之后才采样版本，否则会把检查后刚到达的通知误当成已经处理过。
Wait 在同一把锁内比较版本并进入条件等待：检查期间发生通知时立即返回，睡眠期间发生通知时被唤醒。
多个等待者分别记录自己的版本；一次通知会唤醒所有版本等待者，消息仍由队列保证只被取出一次。
通知允许合并，也可能对应已经被其他消费者取走的消息，因此一次唤醒不保证有一条可消费消息。
Wait 不以队列非空为返回条件：暂停时可以保留消息并睡眠，等待恢复或退出通知。运行时必须先检查并处理待办，不能跳过检查直接等待。
进入排空状态后，Wait 立即返回 `ERR_QUEUE_FLUSHING`，即使队列中仍有消息；调用方仍须用 Consume 取出剩余消息。
单纯 `WakeAll` 会让 Wait 返回；普通阻塞 Consume 若仍无消息且未进入排空状态，会继续等待。

`CODE_OK` 表示唤醒版本已变化，调用方应重新检查业务条件；虚假唤醒后版本未变则继续等待。
`Wait` 不设置超时；有到期需求的业务通过 timer 登记，由到期消息入队触发唤醒。

Snapshot/Wait/WakeAll 使用队列内部锁，正常完成时先释放锁再返回。调用方不得持队列锁调用这些接口、调用 timer 或执行业务；
进入 Wait 前也必须释放通知方需要取得的业务锁。
控制状态必须先完成同步发布再通知，不能只改一个跨线程普通变量。

## 退出和所有权

已初始化队列的运行状态只有正常和排空两种，由 `flushing_flag` 表示并在队列锁内读写。`dominoThreadQueueBeginFlush()` 设置排空状态、推进唤醒版本并唤醒全部等待者；重复调用保持排空状态并再次通知。进入后不提供恢复，销毁并重新 Init 才能再次生产。

| 接口 | 排空状态下的行为 |
| --- | --- |
| `Produce` | 两种模式均返回 `ERR_QUEUE_FLUSHING`，不修改消息或分配 ID；等待空位的生产者被唤醒后也返回此结果 |
| `Consume`／`ConsumeBatch` | 继续按 FIFO 返回已有消息；队列为空时返回 `ERR_QUEUE_FLUSHING`，不再等待 |
| `Wait` | 立即返回 `ERR_QUEUE_FLUSHING`，此结果不表示消息已经取完 |

`BeginFlush` 只设置状态并通知，不消费消息、释放载荷或等待线程退出。模块负责决定剩余消息是处理还是丢弃，出队消息内部另行拥有的资源仍须按所有权释放；内嵌值本身无需释放。

使用 `Snapshot → 检查模块状态 → Wait` 的引擎业务线程采用 `引擎进入 STOPPING → WakeAll → join`，统一通过 `dominoEngineIsStopping()` 检查退出。Wait 被唤醒后返回循环入口观察退出；队列保持正常状态，允许其他线程的最后批次继续投递。待相关投递者和消费者全部结束后，清理线程再调用 `BeginFlush`，逐条取出剩余消息并释放尚未转移的 AI 行为块，最后 Destroy。

使用阻塞 `Consume`／`ConsumeBatch` 的线程不能仅靠模块标志和 `WakeAll` 退出：正常状态下队列仍为空时，它会继续等待。此类线程退出前，先停止并等待全部生产者，再调用 `BeginFlush`；消费者处理完剩余消息，收到 `ERR_QUEUE_FLUSHING` 后退出，调用方 join 后再 Destroy。日志 Stop 使用此流程并刷新输出，日志 Destroy 只关闭文件和释放队列，由所属生命周期入口管理先后顺序。

引擎先进入 `STOPPING`，再通过 `WakeAll` 唤醒 Executor 和 Planner；两个模块不维护本地退出标志。`dominoHumanModuleStop()` 先 join Executor，确保最后一批规划请求已提交，再 join Planner；随后引擎调用 `dominoTimerModuleStop()` join timer。全局退出后 Planner 的提交入口保持可用，直到执行线程结束；Executor inbox 继续接收 Planner 和 timer 的最后投递，timer 目标队列须保持允许生产直到 timer Stop 返回。所有队列和残留消息保留到引擎 Destroy，再由 human 的 PlannerDestroy／ExecutionDestroy 调用 `BeginFlush`、排空并释放尚未转移的 AI 行为块，最后销毁队列。

队列为空只表示元素已取出，消费者可能仍在处理载荷。销毁前必须确保全部使用者已结束访问，并成功 join 负责消费或等待的工作线程。未启用线程的队列由调用方清理载荷后可直接销毁。

`Destroy` 只释放队列同步对象和元素存储，不释放消息内部指针指向的业务资源，也不设置排空状态、唤醒或等待。调用方须保证销毁期间没有其他队列操作，包括正在等待互斥锁的调用。锁内发现 `waiter_count` 非零时直接打印 stderr 并 exit；该检查不能替代调用方保证无并发访问。

`BeginFlush` 和 `WakeAll` 是通知接口，不是 Start／Stop 生命周期入口；两者返回 `void`，空指针或未初始化队列仍不操作，同步故障直接打印 stderr 并 exit。Destroy 则严格检查初始化状态，不采用这种无操作语义。正常状态下非阻塞消费空队列返回 `ERR_QUEUE_EMPTY`，阻塞消费继续等待；排空状态按上表返回 `ERR_QUEUE_FLUSHING`，由所属模块处理这些业务或控制结果。
队列不解释 Freeze/Thaw 或引擎生命周期；控制状态与正常线程同步由所属模块维护。正常销毁释放全部队列资源；初始化发生致命错误时进程已退出，不存在交给 `Destroy` 处理的失败实例。

本模块提供公共通信机制，不检查时钟冻结；human 执行器在批次边界暂停，退出后的统一结算由 human 模块负责。
