# 通用游戏计时器

提供按相对延迟或绝对截止点登记的一次性现实时间通知，以及按业务队列和 `timer_id` 标记取消。业务登记时传入自己的事件生产队列，计时线程把到期记录封装成通用消息投递到该队列，消费与处理完全由业务模块负责。改期使用取消旧 ID 后重新登记；不提供周期任务、原地改期、存档或离线补算。引擎负责协调 time、timer 和业务消费者的启动、冻结、恢复及退出。

```text
业务线程 → 命令队列 → 计时线程（全局堆） → 请求携带的业务事件队列 → 业务线程消费
           入队并通知后返回，ID 写入请求的 timer_id
```

全模块只有一个计时线程和一个最小堆，所有业务队列的计时请求统一入堆。timer 不注册目标，不创建业务事件队列，不保存业务分派函数或上下文，也不提供消费入口。业务自行维护对象与 `timer_id` 的关联，timer 不保存业务对象指针，也不判断任务是否完成。

## 接口

接口声明在 `entry.h`。

| 接口 | 行为 |
| --- | --- |
| `dominoTimerModuleInit()` | 初始化命令队列、空计时堆及同步资源，不创建线程；不接收目标配置或管理业务事件队列 |
| `dominoTimerModuleStart()` | 使用 Init 已准备的资源创建计时线程；由引擎在 Start 阶段调用 |
| `dominoTimerSchedule(timer)` | 按延迟提交请求，入队成功写回请求的非零 `timer_id`；非空请求失败时 `timer_id` 为 0 |
| `dominoTimerScheduleAt(timer, due_ms)` | 精确保留绝对截止点，已到期则尽快异步投递；登记和所有权合同与 Schedule 相同 |
| `dominoTimerCancel(timer_id, event_queue_ptr)` | 在命令队列、全局堆及指定业务队列中查找并标记取消 |
| `dominoTimerNotifyTimeChanged()` | 解冻或引擎进入 `STOPPING` 后唤醒计时线程，不改变时间，也不等待线程回执；普通冻结不调用 |
| `dominoTimerModuleStop()` | 引擎发布全局退出并唤醒后 join 计时线程，保留命令队列、堆及同步资源，无返回值 |
| `dominoTimerModuleDestroy()` | 引擎确认计时线程已结束或从未启动后，释放命令队列、堆及同步资源，并清空整个模块（包括线程句柄存储）；不停止或 join 线程，业务队列由业务方处理 |

Schedule、ScheduleAt 和 Cancel 的 timer 资源及业务队列初始化和有效期由引擎调用链保证，接口不重复检查初始化标志。业务队列指针为空返回 `ERR_NULL_POINTER`，元素大小不是 `sizeof(DominoThreadMessage)` 返回 `ERR_INVALID_PARAM`；请求标记、ID 和毫秒范围等业务校验仍保留。队列可混合保存 AI、timer 和其他消息，容量由业务模块选择。

timer 是引擎内部模块，Init／Start／Stop／Destroy 的顺序由引擎对外入口校验并统一编排，timer 不维护完整生命周期状态或重复校验；时间变更通知直接访问已初始化的同步资源，不增加初始化探测。运行过的实例由引擎先发布 STOPPING 并唤醒，再调用 timer Stop 直接 join；仅 Init 时引擎跳过 timer Stop，在引擎 Stop 完成后直接调用 timer Destroy 释放已准备的资源。Destroy 将模块恢复为空状态，可供下一次 Init 使用。时钟、分配、同步和内部数据损坏等致命错误仍直接通过公共错误出口打印到 `stderr` 并调用 `exit(EXIT_FAILURE)`。不维护实例首错或线程失败回执，也不逐层传播不可恢复的错误。登记参数不合法、延迟相加越界以及取消未找到等业务结果只返回调用方，timer 不重复打印，也不判断调用阶段。由掌握业务语义的调用边界决定拒绝请求或继续执行；确需记录时，运行阶段使用日志宏，非运行阶段统一使用 `DOMINO_PRINT_MESSAGE` 直接写 stdout。当前 human 唤醒适配边界将唤醒登记失败视为致命错误，只通过公共退出方法打印一次；取消未找到作为正常结果处理。

## 数据结构

登记参数、内部命令队列和堆项使用扁平的 `DominoTimer`；到期时它成为通用消息的载荷，大小为 32 字节：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `timer_id` | `domino_timer_id_t`（`uint64_t`） | 首字段，由登记命令队列直接写入；入堆和到期投递保持不变，用于取消和业务校验 |
| `event_queue_ptr` | `DominoThreadQueue*` | 业务方拥有的到期事件队列，登记、取消及投递使用同一队列地址 |
| `due_ms` | `domino_runtime_ms_t`（`uint32_t`） | Schedule 计算或 ScheduleAt 精确传入的累计未冻结现实毫秒绝对到期时间 |
| `delay_ms` | `domino_runtime_ms_t`（`uint32_t`） | Schedule 的相对延迟输入；ScheduleAt 写入登记时剩余延迟，已到期时为 0 |
| `event_type` | `uint8_t` | 业务事件类型 |
| `cancelled_flag` | `bool` | 取消标记 |

登记命令和堆项按值复制；到期时直接构造 `type = DOMINO_THREAD_MESSAGE_TIMER` 的 `DominoThreadMessage`，将 `DominoTimer` 按值放入 `payload.timer`，整条消息复制进业务收件队列。创建、处理、取消跳过或丢弃到期消息均无需分配或释放载荷。队列指针随载荷传递。队列对象及其地址必须保持有效：计时线程退出并 join、业务消费者结束前，不得搬移、销毁或重新初始化队列。

## 登记、取消和消费

`delay_ms` 是从 Schedule 调用时开始计算的现实毫秒延迟，冻结时间不计入，0 表示尽快异步投递。Schedule 直接调用无返回值的 `dominoTimeGetRuntimeMs(&now_ms)`，读取 `domino_runtime_ms_t`（`uint32_t`）累计未冻结现实毫秒；该接口将底层现实纳秒除以 1000000，校验范围后写入 32 位输出，采样失败、累计时间越界或输出指针错误均在 time 模块内打印并终止进程。Schedule 继续检查 `delay_ms <= UINT32_MAX - now_ms` 后计算 `due_ms = now_ms + delay_ms`；请求的延迟超出剩余范围时返回 `ERR_OUT_OF_RANGE`，由调用方处理该业务拒绝。命令排队时间计入延迟，游戏日历倍率只用于显示层。

已持有业务绝对截止点时调用 `dominoTimerScheduleAt(timer, due_ms)`。它复用相同校验和入队流程，仍采样并校验当前业务时刻，随后精确写入传入的 `due_ms`；若尚未到期，`delay_ms` 写为 `due_ms - now_ms`，否则写为 0。过去的截止点和世界起点 0 都保留原值，按已到期事件尽快异步投递；`UINT32_MAX` 同样合法。它不做第二次 `now_ms + delay_ms`，因此不会把调用前已确定的截止点向后平移。冻结期间可以登记，两种登记方式均在解冻后恢复处理，既有绝对截止点不顺延。

`now_ms`、`due_ms`、`delay_ms` 及等待差值均固定使用 32 位现实毫秒，0 和 `UINT32_MAX` 都有效，不回绕、截断、饱和或归零。约 49.71 个累计未冻结现实日、约 248.55 个游戏年的范围是当前产品约定，不是待改为 64 位的缺陷。64 位只保留给底层纳秒、UTC 墙上时间戳和 ID 等各自用途，存档加载后继续累计。读取当前业务时间失败或越界属于致命错误，直接打印并终止进程；调用方请求的延迟相加越界仍返回 `ERR_OUT_OF_RANGE`。

产品要求单次游戏会话最多持续 2 个游戏年对应的现实时间，即 24 分钟 × 12 × 2 = 576 个现实分钟（9.6 小时），届时保存退出、再次加载继续；自动保存退出尚未实现，也不由 timer 负责。会话时长包含玩家冻结期间并单独统计，不把跨存档累计时钟归零，也不把 576 分钟当作累计到期时间的范围。

正常登记时将 `cancelled_flag` 初始化为 false。调用方提供 `delay_ms`、`event_queue_ptr`、`event_type` 和 `cancelled_flag`，这些业务输入字段在 Schedule 中保持不变。每次调用先将请求的 `timer_id` 置为 0，全部校验通过后将计算结果写入请求的 `due_ms`，再直接把调用方的 `timer_ptr` 传给命令队列 Produce，不构造局部计时记录。输入的 `timer_id` 和 `due_ms` 不作为登记依据，请求记录在调用期间须由当前线程独占。已取消请求直接返回 `ERR_INVALID_PARAM`，不入队也不消耗命令 ID。

ScheduleAt 的截止点来自独立的 `due_ms` 参数，原请求的 `due_ms` 和 `delay_ms` 都不作为输入；`event_queue_ptr`、`event_type` 和 `cancelled_flag` 保持不变。校验失败只清零 `timer_id`，原 `due_ms` 和 `delay_ms` 不变。成功入队后的身份生成、取消和载荷移交与 Schedule 完全相同。

命令 Produce 成功时，队列先将新登记 ID 写入调用方请求的首字段 `timer_id`，再把整条记录按值复制入队；调用方和命令副本立即持有同一个非零登记 ID，无需在登记返回后或入堆前复制 ID。计时线程取出命令后直接加入全局堆；到期投递只生成外层 `DominoThreadMessage.id`，内嵌载荷的 `timer_id` 保持不变。重复登记同一记录会覆盖其 `timer_id`，业务仍需使用旧取消凭据时应在下一次登记前保存。

非空请求的校验失败只将 `timer_id` 清为 0，原有 `due_ms` 保持不变。命令队列满、队列已进入排空状态、内存不足或消息 ID 耗尽等提交错误属于致命错误，直接打印并终止进程。提交成功只表示请求已入队；后台入堆的分配失败或容量越界同样直接打印并终止进程。

取消不提交命令、不维护有效 ID 集合，也不删除任意堆节点。登记 ID 在本次 Init 生命周期内唯一，查找按三个位置依次进行：`cancelTimerCommand(timer_id)` 固定遍历命令队列，按 `timer_id` 匹配；随后在全局堆按 `timer_id` 匹配；最后由 `cancelTimerEvent(event_queue_ptr, timer_id)` 遍历指定业务队列，只检查 `type == DOMINO_THREAD_MESSAGE_TIMER` 且 `payload.timer.timer_id` 相同的消息。命令和堆项只按 ID 定位，传入的业务队列仅限定已投递事件的查找范围。两个队列辅助函数均返回 `bool`：找到请求就将 `cancelled_flag` 设为 true 并返回 true，未找到返回 false；堆中找到请求时同样直接设置取消标志。处理时，已取消的命令由计时线程取出后跳过；已取消堆顶直接弹出，尚未成为堆顶的取消项留在堆中；已进入业务队列的取消项由业务消费者取出后跳过。计时线程在批次边界检测到冻结后，命令队列和堆中的取消项保留原位置，直到解冻后由计时线程处理。

主循环在命令处理前获取 `request_mutex`，调用 `processCommandBatchLocked()` 后继续持锁完成本轮等待决策，最后统一释放请求锁。命令处理在请求锁保护下最多调用 64 次 `dominoThreadQueueConsume(..., false)`，直到队列为空或达到批次上限；每次 Consume 分别取得命令队列锁，已取消项跳过，其余逐条入堆。批次不等待凑满命令；处理中已入队的新命令也可能在本批剩余预算内被取出。整批出队与入堆均在请求锁内完成；堆顶取消检查与投递同样持有请求锁，取消不能在请求迁移中插入。投递时先按值复制堆顶并移除堆顶，再把该副本内嵌到通用消息的 `payload.timer`，调用 `dominoThreadQueueProduce(timer.event_queue_ptr, &message, false)`。队列保存完整值副本，成功或失败均无需释放 timer 载荷。任意非 `CODE_OK` 的投递结果均直接打印到 `stderr` 并终止进程；包括队列满、队列已进入排空状态、内存分配失败或消息 ID 耗尽，不保留请求等待重试。

找到请求并设置取消标志后返回 `CODE_OK`，已取消但仍可找到的请求也返回 `CODE_OK`；未知、已移除或已出队的 ID 返回 `ERR_NOT_FOUND`。事件已投递时，传错业务队列也会查找失败；尚在命令队列或堆中的请求仍按 ID 定位。到期载荷直接内嵌于 `DominoThreadMessage`，扫描先检查消息类型，再访问 `payload.timer`。对已投递事件的取消与业务 Consume 在业务队列锁内竞争。业务必须在成功出队后检查 `cancelled_flag`，跳过取消事件；未取消事件一旦出队便取得处理资格，即使尚未开始业务处理，也不能再撤回。业务消费只获取队列锁，不持 timer 请求锁。

业务自行调用 `dominoThreadQueueConsume()`，可以按自身循环选择阻塞或非阻塞模式，依据对象存在性、任务状态和当前 `timer_id` 判断通知是否仍有效。消费或业务处理不需要通知 timer，也不受 timer 的分派函数约束。ID 仅在本次 Init 生命周期内有效；复用业务队列进入下一次 timer Init 前，必须排空旧事件，避免与重新生成的 ID 混淆。

以下调用片段展示业务队列的初始化、登记、消费和取消；业务模块应处理登记、消费和取消的业务返回码，并自行包含
`domino_shared_common.h`、`domino_shared_queue.h` 和 engine 的 `queue/thread_message.h`。timer 首次由静态初值、后续由 Destroy 保证空状态；控制线程在 Init 阶段调用 `dominoTimerModuleInit()` 创建队列和同步资源，不重复清空模块结构。storage 恢复冻结时钟的累计基准后，在 Start 阶段调用 `dominoTimerModuleStart()` 创建计时线程，待所有线程就绪后由引擎解冻并建立单调起点。

```c
static DominoThreadQueue business_inbox;

dominoThreadQueueInit(&business_inbox, sizeof(DominoThreadMessage), DOMINO_THREAD_QUEUE_DEFAULT_CAPACITY,
                      DOMINO_THREAD_QUEUE_MAX_CAPACITY);
```

```c
DominoTimer timer = {
    .event_queue_ptr = &business_inbox,
    .delay_ms = 60000U,  // 现实 1 分钟。
    .event_type = 1U,
    .cancelled_flag = false,
};
DOMINO_CODE code = dominoTimerSchedule(&timer);
```

```c
DominoThreadMessage message;
DOMINO_CODE code = dominoThreadQueueConsume(&business_inbox, &message, false);
if (code == CODE_OK) {
    if (message.type == DOMINO_THREAD_MESSAGE_TIMER) {
        const DominoTimer* timer_ptr = &message.payload.timer;
        if (!timer_ptr->cancelled_flag) {
            // 按 timer_id、event_type 及当前业务状态处理到期通知。
        }
    } else {
        // 按 type 分派 AI 结果、请求或其他命令；AI 消息还须释放未接管的行为块。
    }
}
```

```c
// 取消时传入登记所用的同一业务队列。
DOMINO_CODE code = dominoTimerCancel(timer.timer_id, &business_inbox);
```

## 调度和等待

全局堆按 `(due_ms, timer_id)` 排序，无独立排序序号或 ID 索引。入堆、弹出堆顶为 O(log M)，偶尔扩容需要 O(M)，M 是全局堆中的请求数量；取消查找为 O(N)，N 是命令、堆和指定业务队列中的请求总量。已入堆的请求统一按到期时间选择投递，同刻事件按登记 ID 排序；尚在命令队列中的请求不参与堆排序，各业务线程的实际消费顺序也不由 timer 控制。

堆数组从索引 1 开始存储，`events_ptr[0]` 保留不使用，有效元素范围为 `[1, count]`，堆顶位于索引 1。`capacity` 表示可存储的元素数量，实际分配 `capacity + 1` 个数组槽位；索引 `i` 的父节点为 `i / 2`，左右子节点分别为 `2 * i` 和 `2 * i + 1`。

每轮先通过 `dominoEngineIsStopping()` 检查引擎全局退出，再检查 time 冻结。通过检查后先处理最多 64 个堆项，再处理最多 64 条登记命令，共同构成一批，优先投递已经入堆的到期事件。冻结或普通退出发生在批次内部时，当前批次完整执行，下一轮才停止；检测到致命错误则直接打印并终止进程。取消项的清理也占处理预算，不按业务队列轮转。登记批次输出实际出队条数，包括取消项。本轮只要处理过登记命令或耗尽堆处理预算，就释放请求锁进入下一轮；只有命令数为 0 且堆处理预算未耗尽时，才在仍持请求锁的情况下调用 `waitForWorkLocked()`。命令数为 0 表示本轮第一次 Consume 已返回 `ERR_QUEUE_EMPTY`，等待决策复用这次检查，不再次取得队列锁查询是否为空。

`request_mutex` 保护全局堆、请求迁移和取消，并用于衔接命令队列检查、等待决策与唤醒通知，作为 `wake_condition` 的等待互斥锁。计时线程不维护独立唤醒标志，统一通过 `dominoEngineIsStopping()` 判断引擎是否处于 `STOPPING`，不维护独立退出状态。线程退出由 `thrd_join()` 同步，不维护停止回执、停止结果或运行首错；也不维护冻结确认标志、冻结条件变量或冻结回执接口。需要同时持锁时，顺序固定为请求锁后队列锁；业务直接消费只持业务队列锁，不获取请求锁。

Schedule 和 ScheduleAt 先执行命令队列的非阻塞 Produce，队列内部锁释放后，入队成功才取得 `request_mutex` 并发出信号；不能持有队列锁再获取请求锁。非阻塞入队只表示不等待队列腾出空间，入队后的通知可能等待正在进行的命令批次、堆处理、等待决策或取消扫描，但不等待该请求入堆或到期。Cancel 和时间通知同样可能短暂等待当前持锁操作结束，但时间通知不等待线程冻结或恢复回执。

登记成功、取消成功和解冻通知会在请求锁内唤醒计时线程；普通冻结不通知计时线程。Cancel 成功后直接使用已经持有的请求锁通知。引擎退出先进入 `STOPPING`，再通过相同通知入口唤醒计时线程。timer Stop 要求引擎已发布全局退出并完成唤醒，只负责 join 计时线程，不发布退出、重复唤醒或轮询停止回执；timer Destroy 由引擎在自身 Stop 完成后调用，只释放资源；仅 Init 时计时线程从未创建，无需调用 timer Stop。

计时线程在每轮开始持请求锁检查冻结，发现冻结时在同一锁上条件等待；等待谓词直接检查引擎全局退出，因此暂停期间退出不需要解冻业务时钟。登记或取消通知只使线程重新检查冻结及退出状态，仍冻结则继续等待，不检查堆顶或取消标记。

`waitForWorkLocked()` 由调用方持续持有请求锁，在已完成本轮命令队列空检查的前提下复查退出、冻结和全局堆顶，不检查业务队列容量。堆顶已取消或已经到期就跳过等待；堆空则条件等待；有未来堆顶则按剩余现实毫秒直接计算等待间隔，使用标准 `cnd_timedwait()`。从命令队列空检查到调用条件等待，中间不释放请求锁。新命令仍可在此期间入队；若本轮进入条件等待，提交方只能在等待操作原子释放请求锁并进入等待后取得请求锁、发出唤醒信号，因此无需额外判空或保存通知标志。检查前已入队的命令会由本轮 Consume 取出，并使本轮跳过等待。

工作等待中的 `cnd_wait()` 和 `cnd_timedwait()` 唤醒后重新取得请求锁，由主循环统一解锁，再返回循环头部先检查退出、再检查冻结。UTC 系统时钟调整可能改变一次等待的长度，到期判断仍以 time 模块累计未冻结的现实时间为准。

## 冻结和退出

普通冻结由客户端直接调用 time 固定业务时钟，不向主循环发控制请求、不通知计时线程、不等待冻结回执。计时线程只在批次边界观察冻结，当前批次可以继续迁移命令、回收堆项和投递；随后等待解冻或退出。原本等待命令或计时的线程保持等待，到期或收到其他通知时重新检查冻结，无须为暂停单独唤醒。冻结成功只表示业务时钟停止，不表示业务数据或队列已经停止变化。日志模块和消息队列本身不受冻结影响。

Schedule、ScheduleAt 和 Cancel 在冻结期间仍允许提交及标记请求，引擎发布普通退出后也允许尚未完成的业务批次继续调用，直到调用方结束。尚未处理的命令、取消项和已投递事件保留；timer 不维护另一套暂停开关，也不等待业务处理结束。

存档累计基准加载完成、时钟保持冻结且引擎进入 `STARTING` 后才可启动 timer。timer 的固定资源已在引擎 Init 中准备，客户端在冻结时钟下先启动 logger 和 human 执行／规划线程，再调用 `dominoTimerModuleStart()`，最后创建主循环；主循环不负责启动其他线程。成功后客户端直接解冻 time，再调用 `dominoTimerNotifyTimeChanged()`，让线程重新检查时钟和等待截止点；它只通知，不修改 time 或平移已有截止点。启动中的致命错误直接打印并终止进程。普通恢复继续处理已有请求，不需要清空重建计时器或全员重新登记。首次启动和加载存档的调度初始化由业务模块单独负责。

生命周期接口由同一客户端串行调用；普通冻结、解冻及退出停钟均由客户端直接写入 time，保证这些时间写入不并发。致命错误在检测处直接打印并终止进程，无单独的失败通知接口或后台错误传播。正常退出顺序：

1. 客户端调用 `dominoEngineStop()`，先进入 `STOPPING`、冻结并校验业务时刻，再直接唤醒主循环、业务线程和 timer，无需解冻。各线程先检查退出再检查暂停；已经开始的批次完成后退出。此时 timer 命令队列、计时堆和业务事件队列全部保活，允许尚未结束的业务批次登记、取消及投递。
2. 客户端先 join 主循环，再调用 `dominoHumanModuleStop()` join 执行和规划线程，结束所有 Schedule、ScheduleAt 和 Cancel 调用；timer 的目标业务队列保持正常状态，继续允许投递。随后调用 `dominoTimerModuleStop()` join 计时线程，保留 timer 命令队列、堆和同步资源。Stop 不与其他 timer API 并发。
3. 客户端确认业务线程全部结束后，统一将人物结算到固定时刻，再调用 logger Stop 排空日志、join writer 并刷新输出，进入 `STOPPED` 并返回。所有模块的队列、留队消息、关联表和同步对象均保留，尚未保存。
4. 客户端随后显式调用 `dominoEngineDestroy()`，进入 `DESTROYING`，先调用 timer Destroy 并释放主循环队列，准备导航并保存世界，再调用 human Destroy 和其余世界模块的 Destroy，最后关闭日志文件并释放日志队列。仅 Init、未 Start 的实例同样由外部先 Stop 再 Destroy；此时所有线程尚未创建，引擎跳过 human／timer Stop、全部 join 和人物结算，仍调用 logger Stop 关闭入队并刷新输出，Destroy 不停止任何线程。human Destroy 对收件队列调用 `dominoThreadQueueBeginFlush()`，取出剩余消息并释放尚未转移的 AI 行为块，再销毁队列、关联表和同步对象。内嵌的 timer 和命令载荷无需释放；队列 Destroy 不会释放 AI 行为指针指向的块。各模块 Destroy 不调用 Stop；计时线程和业务消费者全部 join 后，才可销毁业务队列，下一次 timer Init 前须排空旧事件。主循环不负责退出停钟或协调收尾；普通暂停不负责全员结算或提供可保存快照。

timer 堆由 `dominoTimerHeapDestroy()` 释放事件数组并恢复为空堆，由 `dominoTimerModuleDestroy()` 统一调用。Destroy 丢弃尚未投递的命令和堆项，销毁同步对象后立即清空整个 timer 模块，不保留旧线程句柄存储；业务队列的 BeginFlush、载荷清理和销毁由所属模块负责。引擎在保存和世界资源清理完成后单独重置 time 内存快照，已保存的累计时间不受影响。引擎公共生命周期调用错误，以及 timer 资源操作和计时线程同步原语失效，均直接打印到 `stderr` 并 `exit`，业务时钟、入堆或投递的致命错误采用同一出口。该出口不依赖异步日志队列；`exit` 允许标准流清理及已注册的退出处理，退出处理必须避免等待当前故障线程或重入故障资源。

## 验证范围

当前说明基于源码合同，尚无性能实测，也未验证引擎整体初始化、冻结恢复及退出接入。源码或格式检查不代表线程竞态、运行行为或容量已经验证。
