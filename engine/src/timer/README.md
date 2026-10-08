# 通用游戏计时器

提供一次性现实时间通知，以及按业务队列和 `timer_id` 标记取消。业务登记时传入自己的事件生产队列，计时线程将到期记录直接投递到该队列，消费与处理完全由业务模块负责。改期使用取消旧 ID 后重新登记；不提供周期任务、原地改期、存档或离线补算。当前未接入引擎生命周期、human 或 time 状态变更的通知流程。

```text
业务线程 → 命令队列 → 计时线程（全局堆） → 请求携带的业务事件队列 → 业务线程消费
           入队并通知后返回，ID 写入请求的 timer_id
```

全模块只有一个计时线程和一个最小堆，所有业务队列的计时请求统一入堆。timer 不注册目标，不创建业务事件队列，不保存业务分派函数或上下文，也不提供消费入口。业务自行维护对象与 `timer_id` 的关联，timer 不保存业务对象指针，也不判断任务是否完成。

## 接口

接口声明在 `entry.h`。

| 接口 | 行为 |
| --- | --- |
| `dominoTimerModuleInit()` | 初始化命令队列及同步资源后创建计时线程；不接收目标配置或管理业务事件队列 |
| `dominoTimerSchedule(timer)` | 按延迟提交请求，入队成功写回请求的非零 `timer_id`；非空请求失败时 `timer_id` 为 0 |
| `dominoTimerCancel(timer_id, event_queue_ptr)` | 在命令队列、全局堆及指定业务队列中查找并标记取消 |
| `dominoTimerNotifyTimeChanged()` | 通知计时线程重新检查累计未冻结现实时间，不改变时间或业务消费状态 |
| `dominoTimerModuleExit()` | 通知计时线程退出并 join，释放命令队列、堆及同步资源；业务队列由业务方处理 |

Schedule 和 Cancel 要求业务队列指针非空、已经初始化，且元素大小为 `sizeof(DominoTimer)`；分别返回 `ERR_NULL_POINTER`、`ERR_NOT_INITIALIZED` 或 `ERR_INVALID_PARAM`。队列只能存储 `DominoTimer`，容量由业务模块选择。Init 的任何资源初始化失败都会清理已创建的部分资源。

## 数据结构

登记参数、命令队列、堆项和到期事件统一使用扁平的 `DominoTimer`，大小为 40 字节：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `id` | `uint64_t` | 首 8 字节的队列消息 ID，每次入队由队列重新分配 |
| `timer_id` | `domino_timer_id_t`（`uint64_t`） | 稳定的计时器 ID，用于取消和业务校验 |
| `event_queue_ptr` | `DominoThreadQueue*` | 业务方拥有的到期事件队列，登记、取消及投递使用同一队列地址 |
| `due_ms` | `domino_runtime_ms_t`（`uint32_t`） | Schedule 计算的累计未冻结现实毫秒下的绝对到期时间 |
| `delay_ms` | `domino_runtime_ms_t`（`uint32_t`） | 调用方提交的相对现实毫秒延迟 |
| `event_type` | `uint8_t` | 业务事件类型 |
| `cancelled_flag` | `bool` | 取消标记 |

记录按值复制，队列指针随记录进入命令队列、堆和业务队列。队列对象及其地址必须保持有效：计时线程退出并 join、业务消费者结束前，不得搬移、销毁或重新初始化队列。

## 登记、取消和消费

`delay_ms` 是从 Schedule 调用时开始计算的现实毫秒延迟，冻结时间不计入，0 表示尽快异步投递。Schedule 通过 `dominoTimeGetRuntimeMs(&now_ms)` 读取 `domino_runtime_ms_t`（`uint32_t`）累计未冻结现实毫秒；该接口将底层现实纳秒除以 1000000，先校验范围，再写入 32 位输出，成功返回 `CODE_OK`，越界返回 `ERR_OUT_OF_RANGE` 且不改写输出。Schedule 继续检查 `delay_ms <= UINT32_MAX - now_ms` 后计算 `due_ms = now_ms + delay_ms`，超出范围直接返回错误。命令排队时间计入延迟，游戏日历倍率只用于显示层。

`now_ms`、`due_ms`、`delay_ms` 及等待差值均固定使用 32 位现实毫秒，0 和 `UINT32_MAX` 都有效，不回绕、截断、饱和或归零。约 49.71 个累计未冻结现实日、约 248.55 个游戏年的范围是当前产品约定，不是待改为 64 位的缺陷。64 位只保留给底层纳秒、UTC 墙上时间戳和 ID 等各自用途，存档加载后继续累计。后台读取时间越界时记录错误并 `abort()`，不会继续用超出业务范围的时间判断到期。

产品要求单次游戏会话最多持续 8 个现实小时，届时保存退出、再次加载继续；当前 timer 不实现自动保存退出。会话时长单独统计，不把跨存档累计时钟归零，也不把 8 小时当作累计到期时间的范围。

正常登记时将 `cancelled_flag` 初始化为 false。调用方提供 `delay_ms`、`event_queue_ptr`、`event_type` 和 `cancelled_flag`，这些业务输入字段在 Schedule 中保持不变。每次调用先将请求的 `timer_id` 置为 0，全部校验通过后将计算结果写入请求的 `due_ms`，再直接把调用方的 `timer_ptr` 传给命令队列 Produce，不构造局部计时记录。输入的 `id`、`timer_id` 和 `due_ms` 不作为登记依据，请求记录在调用期间须由当前线程独占。已取消请求直接返回 `ERR_INVALID_PARAM`，不入队也不消耗命令 ID。

命令 Produce 成功时，队列先将新消息 ID 写回调用方请求的 `id`，再把整条记录按值复制入队；此时队列副本的 `timer_id` 仍为 0。Produce 返回后，Schedule 将调用方请求的 `timer_id` 设为 `id`，作为取消凭据。计时线程取出命令后设置副本的 `timer_id = id`，再加入全局堆；到期事件入队只重新分配 `id`，保留稳定的 `timer_id`。重复登记同一记录会覆盖其 `id` 和 `timer_id`，业务仍需使用旧取消凭据时应在下一次登记前保存。

非空请求的校验失败只将 `timer_id` 清为 0，原有 `id` 和 `due_ms` 保持不变。命令入队失败时不消耗消息 ID，调用方的 `id` 保持原值、`timer_id` 为 0，但已经计算并写入的 `due_ms` 保留；命令队列满或内存不足等提交错误直接返回调用方。提交成功只表示请求已入队，后台入堆失败时记录业务队列地址、timer ID 和错误码，然后调用 `abort()` 立即终止程序。

取消不提交命令、不维护有效 ID 集合，也不删除任意堆节点。它按队列指针和 ID 同时匹配：先在命令队列按 `timer.id` 查找，再在全局堆及传入的业务队列按 `timer_id` 线性查找，将匹配请求的标志设为 true。处理时，已取消的命令由计时线程取出后跳过；已取消堆顶直接弹出，尚未成为堆顶的取消项留在堆中；已进入业务队列的取消项由业务消费者取出后跳过。主循环检测到冻结后，命令队列和堆中的取消项保留原位置，直到解冻后由计时线程处理。

命令处理每批只获取和释放一次 `request_mutex`，整批出队与入堆均在锁内完成；堆顶取消检查与投递同样持有请求锁，取消不能在请求迁移中插入。投递时先按值复制堆顶并移除堆顶，再直接调用 `dominoThreadQueueProduce(timer.event_queue_ptr, &timer, false)`。任意非 `CODE_OK` 的投递结果均记录业务队列地址（`%p`）、timer ID 和错误码并调用 `abort()`；包括队列满、生产端停止、内存分配失败或消息 ID 耗尽，不保留请求等待重试。

取消成功返回 `CODE_OK`；未知、重复取消、队列不匹配或已出队的 ID 返回 `ERR_NOT_FOUND`。对已投递事件的取消与业务 Consume 在业务队列锁内竞争。业务必须在成功出队后检查 `cancelled_flag`，跳过取消事件；未取消事件一旦出队便取得处理资格，即使尚未开始业务处理，也不能再撤回。业务消费只获取队列锁，不持 timer 请求锁。

业务自行调用 `dominoThreadQueueConsume()`，可以按自身循环选择阻塞或非阻塞模式，依据对象存在性、任务状态和当前 `timer_id` 判断通知是否仍有效。消费或业务处理不需要通知 timer，也不受 timer 的分派函数约束。ID 仅在本次 Init 生命周期内有效；复用业务队列进入下一次 timer Init 前，必须排空旧事件，避免与重新生成的 ID 混淆。

以下调用片段展示业务队列的初始化、登记、消费和取消；业务模块应处理各步返回码。控制线程单独调用 `dominoTimerModuleInit()`，并确保 time 已准备好后再登记。

```c
static DominoThreadQueue business_timer_queue;

DOMINO_CODE code = dominoThreadQueueInit(&business_timer_queue, sizeof(DominoTimer), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                       DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
```

```c
DominoTimer timer = {
    .event_queue_ptr = &business_timer_queue,
    .delay_ms = 60000U,  // 现实 1 分钟。
    .event_type = 1U,
    .cancelled_flag = false,
};
DOMINO_CODE code = dominoTimerSchedule(&timer);
```

```c
DominoTimer event;
DOMINO_CODE code = dominoThreadQueueConsume(&business_timer_queue, &event, false);
if (code == CODE_OK && !event.cancelled_flag) {
    // 业务方按 event.timer_id 与 event.event_type 校验并处理自己的对象。
}
```

```c
// 取消时传入登记所用的同一业务队列。
DOMINO_CODE code = dominoTimerCancel(timer.timer_id, &business_timer_queue);
```

## 调度和等待

全局堆按 `(due_ms, timer_id)` 排序，无独立排序序号或 ID 索引。入堆、弹出堆顶为 O(log M)，偶尔扩容需要 O(M)，M 是全局堆中的请求数量；取消查找为 O(N)，N 是命令、堆和指定业务队列中的请求总量。已入堆的请求统一按到期时间选择投递，同刻事件按登记 ID 排序；尚在命令队列中的请求不参与堆排序，各业务线程的实际消费顺序也不由 timer 控制。

堆数组从索引 1 开始存储，`events_ptr[0]` 保留不使用，有效元素范围为 `[1, count]`，堆顶位于索引 1。`capacity` 表示可存储的元素数量，实际分配 `capacity + 1` 个数组槽位；索引 `i` 的父节点为 `i / 2`，左右子节点分别为 `2 * i` 和 `2 * i + 1`。

冻结检查只在每轮头部执行，发现冻结时仅条件等待，不访问命令队列或堆。通过检查后先处理最多 64 个堆项，再处理最多 64 条登记命令，优先投递已经入堆的到期事件；冻结发生在本轮处理中时，本轮可能完成。取消项的清理也占处理预算，不按业务队列轮转。登记批次返回实际出队条数，包括取消项；入堆失败立即终止程序。本轮只要处理过登记命令或耗尽堆处理预算，就立即进入下一轮重新检查冻结和堆顶，否则根据当前堆顶、累计现实时间和唤醒状态决定是否等待。

`request_mutex` 保护全局堆、请求迁移、取消查找、`wake_pending_flag` 和 `stop_requested_flag`，并作为 `wake_condition` 的等待互斥锁。需要同时持锁时，顺序固定为请求锁后队列锁；业务直接消费只持业务队列锁，不获取请求锁。

Schedule 先执行命令队列的非阻塞 Produce，队列内部锁释放后，入队成功才取得 `request_mutex`，设置唤醒标志并发出信号；不能持有队列锁再获取请求锁。非阻塞入队只表示不等待队列腾出空间，入队后的通知可能等待正在进行的命令批次、堆处理或取消扫描，但不等待该请求入堆或到期。Cancel、时间通知和 Exit 同样可能等待当前命令批次释放请求锁。

登记成功、取消成功和时间状态变更通知会在请求锁内将 `wake_pending_flag` 置为 true 并唤醒计时线程；Cancel 成功后直接使用已经持有的请求锁通知。Exit 在同一锁内设置 `stop_requested_flag` 并发出唤醒信号，不设置 `wake_pending_flag`，解锁后 join。

计时线程在每轮检查工作前持请求锁清除唤醒标志，发现冻结时在同一锁上条件等待；登记或取消通知只使线程重新检查冻结及停止状态，仍冻结则继续等待，不检查堆顶或取消标记。进入工作等待函数后，先在请求锁内检查停止、唤醒标志；期间收到通知就返回主循环，由循环头部检查冻结状态，避免通知恰好发生在睡眠前而被漏掉。没有待处理通知时，等待判断只读取全局堆顶，不检查业务队列容量。堆顶已取消或已经到期就跳过等待；堆空则条件等待；有未来堆顶则按剩余现实毫秒直接计算等待间隔，使用标准 `cnd_timedwait()`。`cnd_wait()` 和 `cnd_timedwait()` 原子释放 `request_mutex`，唤醒后重新取得该锁。醒来后返回循环头部检查冻结，再处理工作。UTC 系统时钟调整可能改变一次等待的长度，到期判断仍以 time 模块累计未冻结的现实时间为准。

## 冻结和退出

计时线程只在主循环头部检查 time 冻结状态，发现冻结后不出队命令、不入堆、不检查或回收取消堆顶，也不投递，只等待解冻或退出。Schedule 和 Cancel 仍允许业务提交及标记请求，尚未处理的命令和取消项保留至解冻后处理；业务消费是否暂停由业务循环决定。timer 不维护另一套暂停状态，也不等待业务处理结束。冻结状态与 timer 请求锁没有串行写入或暂停确认，已经开始的一轮可能完成，到下一轮头部才进入冻结等待。

time 完成初始化、冻结或解冻后，调用方应调用 `dominoTimerNotifyTimeChanged()`，让线程重新检查时钟和等待截止点。该接口只通知，不改变 time；当前 time 和 engine 代码尚未接入此调用。

生命周期接口由控制线程串行调用。退出顺序：

1. 调用方先结束所有 Schedule、NotifyTimeChanged 和 Cancel 调用，阻止后续 timer API 调用；业务队列保持原地址、有效且生产端可用。
2. 控制线程调用 Exit，在请求锁内设置停止标志并唤醒计时线程，解锁后 join。计时线程在主循环头部检查停止标志，两个批处理函数及批次之间不重复检查；已开始的一轮可能完成最多 64 个堆项和 64 条命令，再在下一轮头部退出。睡眠前仍检查停止标志，避免退出通知已发出后再次睡眠。Exit 返回前仍可能投递当前轮事件，因此不能提前停止业务队列生产端。join 后丢弃尚未投递的命令和堆项，释放 timer 自有资源；不排空、不停止、不销毁业务队列。
3. Exit 返回后，由业务方决定消费或丢弃剩余事件。结束阻塞消费者时，按队列 `Stop → WakeAll → join → Destroy` 清理：只停止生产端可让消费者排空已有事件后退出；停止双端则禁止继续消费。业务消费者也可提前结束，但队列及生产端仍须保留至 Exit 返回，后续由业务方清理已投递事件。

Exit 不与其他 timer API 并发。计时线程 join 且业务消费者全部结束后，才可搬移、销毁或重新初始化业务队列；下一次 timer Init 前须排空旧事件。

## 验证范围

当前说明基于源码合同，尚无性能实测，也未验证引擎整体初始化、冻结恢复及退出接入。源码或格式检查不代表线程竞态、运行行为或容量已经验证。
