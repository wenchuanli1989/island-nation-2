# 线程通信

`DominoThreadQueue` 提供 FIFO 存储、控制唤醒和截止点等待；跨模块通信使用统一元素 `DominoThreadMessage`。
执行器、规划器等线程各自拥有一个收件队列，其他线程可向它发送 AI、timer、命令等不同类型的消息。
每条消息的 `items_type` 决定 `items_ptr` 指向什么；队列不按业务类型拆分，也不执行业务分派。

| 字段 | 职责 |
| --- | --- |
| `id` | 收件队列生成的消息 ID，与载荷里的请求 ID、timer ID 独立 |
| `items_type` | TIMER、AI_REQUEST、AI_RESULT、COMMAND 等类型 |
| `items_ptr` | 接收者将取得所有权的连续载荷块 |
| `items_size` | 载荷字节数，转换指针之前按业务结构校验 |

`queue.items_ptr` 是环形数组本身，存放 `DominoThreadMessage` 描述符；`message.items_ptr` 才是该条消息的业务载荷。
底层队列仍允许模块内部按固定记录传值，例如 timer 自有的登记命令队列。
等待和唤醒直接使用收件队列已有的互斥锁与条件变量；无需额外对象或绑定关系。队列不解释模块的业务状态。

## 消息发送与接收

收件队列以 `sizeof(DominoThreadMessage)` 初始化。发送者用 `dominoThreadMessageInitCopy()` 复制栈上的记录，
或者直接把独占的、可用 `free` 释放的连续块赋给 `items_ptr`，同时填写类型和大小。
InitCopy 只复制当前块，不递归复制内部指针；大草案可直接移交已经构造好的独占块。

```c
DominoThreadMessage message = {0};
DOMINO_CODE code = dominoThreadMessageInitCopy(&message, DOMINO_THREAD_MESSAGE_COMMAND, &command, sizeof(command));
if (code == CODE_OK) {
    code = dominoThreadQueueProduce(&worker_inbox, &message, false);
    if (code != CODE_OK) {
        dominoThreadMessageRelease(&message);  // 发送失败，载荷仍归发送者。
    }
    // 成功后不再访问或释放 message.items_ptr；局部描述符本身可以丢弃。
}
```

接收者 Consume 得到描述符，按 `items_type` 分派并校验 `items_size`，处理后调用 `dominoThreadMessageRelease()`。
取消、拒绝、未知类型和退出丢弃也必须释放载荷；存在内部另行拥有的资源时，由业务先处理那些资源。
不能把栈地址或可扩容数组内部地址直接交给异步消费者。发送成功后原描述符不再拥有载荷，不能用它再次发送同一指针。

执行器收件队列可依次出现 `AI_RESULT → TIMER → COMMAND`，规划器收件队列也可以混合接收请求、timer 和其他命令。
消息保留 FIFO；抢占、紧急提升和任务选择由接收模块处理。
暂停、恢复、退出请求及处理完成的确认由所属模块维护，使用原子变量或模块自己的锁同步。

## 初始化和唤醒

初始化收件队列后即可启动线程。只需阻塞收发的模块可继续使用 `Consume(..., true)`；
需要响应控制状态或截止点的模块使用 `Snapshot → 检查状态和消息 → Wait`。

- 每次成功 `Produce` 都在队列锁内推进本队列的唤醒版本并通知等待者，包含 timer 投递 TIMER 消息的路径。
- 入队失败、出队、`Stop` 和 `Resume` 不推进版本；出队仍通知等待空位的生产者。
- `WakeAll` 推进本队列版本，并通知全部队列等待者，不占用任何队列容量。
- 模块先发布新的控制状态，再调用 `dominoThreadQueueWakeAll()`；状态与处理完成的确认由模块维护。
- 每个队列只唤醒自身等待者。成功入队时如有已登记等待者，统一广播 `not_empty`，让版本等待和阻塞消费都能重新检查条件；
  统一用 `waiter_count` 登记条件等待，不再区分等待者类型。多个消费者被唤醒后仍通过队列锁竞争消息。

timer 到期时发送 `items_type = DOMINO_THREAD_MESSAGE_TIMER`、`items_ptr` 指向 `DominoTimer` 的通用消息，与 AI、命令共用收件队列。
timer 内部用于取消、堆迁移和计时线程等待的同步协议不变。

## 消费者循环

每轮严格按以下顺序执行：

1. 用 `dominoThreadQueueSnapshot()` 记录本轮 `wake_epoch`。
2. 读取模块控制状态，处理退出，或执行暂停交接并确认；暂停时跳过普通业务处理。
3. 可以运行时，非阻塞地按有限批次消费收件队列，按消息类型分派，并处理本地就绪工作。
4. 仍有立即可做的工作则进入下一轮；否则调用 `dominoThreadQueueWait()`，传入第 1 步的版本值。
5. 通知或超时返回 `CODE_OK` 时，从第 1 步重新检查；端点关停错误进入模块退出或排空流程。

不能在检查队列之后才采样版本，否则会把检查后刚到达的通知误当成已经处理过。
Wait 在同一把锁内比较版本并进入条件等待：检查期间发生通知时立即返回，睡眠期间发生通知时被唤醒。
多个等待者分别记录自己的版本；一次通知会唤醒所有版本等待者，消息仍由队列保证只被取出一次。
通知允许合并，也可能对应已经被其他消费者取走的消息，因此一次唤醒不保证有一条可消费消息。
Wait 不以队列非空为返回条件：暂停时可以保留消息并睡眠，等待恢复或退出通知。运行时必须先检查并处理待办，不能跳过检查直接等待。
任一端关停时，Wait 返回对应关停错误，消费端优先。生产端关停后，Consume 仍可排空已有消息。
单纯 `WakeAll` 会让 Wait 返回；普通阻塞 Consume 若仍无消息且端点未停止，会继续等待。

本地重试等有截止点的工作，传入 `TIME_UTC` 的绝对 `timespec`；无截止点时传 `nullptr`。
`CODE_OK` 表示收到通知或等待超时，调用方都应重新检查业务条件。
虚假唤醒不会重置截止点。UTC 时钟变化可能延长或缩短等待，业务到期仍用已校验的 `domino_runtime_ms_t` 判断；
队列不把业务累计毫秒解释为 UTC，也不替代 time 或 timer。

Snapshot/Wait/WakeAll 使用队列内部锁，返回时已释放。调用方不得持队列锁调用这些接口、调用 timer 或执行业务；
进入 Wait 前也必须释放通知方需要取得的业务锁。
控制状态必须先完成同步发布再通知，不能只改一个跨线程普通变量。

## 退出和所有权

队列不自动暂停、停止或 join 线程。模块收到请求后完成业务交接并发布确认，通知不等于确认。

普通队列仍遵循 `Stop → WakeAll → join → Destroy`；恢复后需要唤醒旧等待者时显式调用 `WakeAll`。
只停生产端可继续排空；双端停止禁止出队。队列销毁仅释放元素存储，不释放消息指向的业务载荷。

先让所有生产者停止使用目标队列，再让消费者排空并释放剩余载荷、退出和 join，然后销毁队列。
选择双端停止时应先 join 消费者，再在无并发访问时只恢复消费端，由清理线程排空并释放剩余载荷；生产端保持停止。
timer 目标队列须保持生产可用直到 `dominoTimerModuleExit()` 返回。
Destroy 会关停双端、唤醒并等待已经登记的版本等待和阻塞出入队操作退出。调用方仍须阻止新操作，并排除正在等待队列互斥锁的调用。

本模块提供公共通信机制，human 执行器及其暂停结算屏障仍由 human 模块后续接入。
