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
| `dominoTimerModuleInit()` | 初始化命令队列及同步资源后创建计时线程，返回 `DOMINO_CODE`；不接收目标配置或管理业务事件队列 |
| `dominoTimerSchedule(timer)` | 按延迟提交请求，入队成功写回请求的非零 `timer_id`；非空请求失败时 `timer_id` 为 0 |
| `dominoTimerScheduleAt(timer, due_ms)` | 精确保留绝对截止点，已到期则尽快异步投递；登记和所有权合同与 Schedule 相同 |
| `dominoTimerCancel(timer_id, event_queue_ptr)` | 在命令队列、全局堆及指定业务队列中查找并标记取消 |
| `dominoTimerNotifyTimeChanged()` | 解冻或引擎退出标志发布后唤醒计时线程，不改变时间，也不等待线程回执；普通冻结不调用 |
| `dominoTimerModuleExit()` | 通知计时线程退出并 join，释放命令队列、堆及同步资源，返回 `DOMINO_CODE`；业务队列由业务方处理 |

Schedule、ScheduleAt 和 Cancel 要求业务队列指针非空、已经初始化，且元素大小为 `sizeof(DominoThreadMessage)`；分别返回 `ERR_NULL_POINTER`、`ERR_NOT_INITIALIZED` 或 `ERR_INVALID_PARAM`。队列可混合保存 AI、timer 和其他消息，容量由业务模块选择。

Init 和 Exit 的资源生命周期错误直接返回调用方，由 engine 的公共生命周期入口记录并返回宿主；入口与等待回执同时检查实例首错，已失败实例不再启动或清理。已有资源原样保留，不回滚、不失败清理、不重试，也不在引擎内终止进程。宿主报告错误并结束进程，此前须保活残留线程可访问的资源，不能再次调用 Init 或 Exit 尝试恢复。正常 Exit 成功时照常释放资源；对未初始化或已经成功退出的 timer 调用 Exit 返回 `CODE_OK`。运行期登记、取消、调度和条件等待的错误约定保持各自合同。

## 数据结构

登记参数、内部命令队列和堆项使用扁平的 `DominoTimer`；到期时它成为通用消息的载荷，大小为 40 字节：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `id` | `uint64_t` | 登记命令队列分配的消息 ID；到期投递不修改它 |
| `timer_id` | `domino_timer_id_t`（`uint64_t`） | 稳定的计时器 ID，用于取消和业务校验 |
| `event_queue_ptr` | `DominoThreadQueue*` | 业务方拥有的到期事件队列，登记、取消及投递使用同一队列地址 |
| `due_ms` | `domino_runtime_ms_t`（`uint32_t`） | Schedule 计算或 ScheduleAt 精确传入的累计未冻结现实毫秒绝对到期时间 |
| `delay_ms` | `domino_runtime_ms_t`（`uint32_t`） | Schedule 的相对延迟输入；ScheduleAt 写入登记时剩余延迟，已到期时为 0 |
| `event_type` | `uint8_t` | 业务事件类型 |
| `cancelled_flag` | `bool` | 取消标记 |

登记命令和堆项按值复制；到期时 `dominoThreadMessageInitCopy()` 创建独立的 `DominoTimer` 载荷，外层 `DominoThreadMessage` 以 `items_type = DOMINO_THREAD_MESSAGE_TIMER`、`items_size = sizeof(DominoTimer)` 进入业务收件队列。发送成功后载荷归消费者，处理、取消跳过或丢弃后均调用 `dominoThreadMessageRelease()`。队列指针随载荷传递。队列对象及其地址必须保持有效：计时线程退出并 join、业务消费者结束前，不得搬移、销毁或重新初始化队列。

## 登记、取消和消费

`delay_ms` 是从 Schedule 调用时开始计算的现实毫秒延迟，冻结时间不计入，0 表示尽快异步投递。Schedule 通过 `dominoTimeGetRuntimeMs(&now_ms)` 读取 `domino_runtime_ms_t`（`uint32_t`）累计未冻结现实毫秒；该接口将底层现实纳秒除以 1000000，先校验范围，再写入 32 位输出，成功返回 `CODE_OK`，越界返回 `ERR_OUT_OF_RANGE` 且不改写输出。Schedule 继续检查 `delay_ms <= UINT32_MAX - now_ms` 后计算 `due_ms = now_ms + delay_ms`，超出范围直接返回错误。命令排队时间计入延迟，游戏日历倍率只用于显示层。

已持有业务绝对截止点时调用 `dominoTimerScheduleAt(timer, due_ms)`。它复用相同校验和入队流程，仍采样并校验当前业务时刻，随后精确写入传入的 `due_ms`；若尚未到期，`delay_ms` 写为 `due_ms - now_ms`，否则写为 0。过去的截止点和世界起点 0 都保留原值，按已到期事件尽快异步投递；`UINT32_MAX` 同样合法。它不做第二次 `now_ms + delay_ms`，因此不会把调用前已确定的截止点向后平移。冻结期间可以登记，两种登记方式均在解冻后恢复处理，既有绝对截止点不顺延。

`now_ms`、`due_ms`、`delay_ms` 及等待差值均固定使用 32 位现实毫秒，0 和 `UINT32_MAX` 都有效，不回绕、截断、饱和或归零。约 49.71 个累计未冻结现实日、约 248.55 个游戏年的范围是当前产品约定，不是待改为 64 位的缺陷。64 位只保留给底层纳秒、UTC 墙上时间戳和 ID 等各自用途，存档加载后继续累计。后台读取时间越界时发布实例首错并退出计时线程，不会继续用超出业务范围的时间判断到期。

产品要求单次游戏会话最多持续 2 个游戏年对应的现实时间，即 24 分钟 × 12 × 2 = 576 个现实分钟（9.6 小时），届时保存退出、再次加载继续；自动保存退出尚未实现，也不由 timer 负责。会话时长包含玩家冻结期间并单独统计，不把跨存档累计时钟归零，也不把 576 分钟当作累计到期时间的范围。

正常登记时将 `cancelled_flag` 初始化为 false。调用方提供 `delay_ms`、`event_queue_ptr`、`event_type` 和 `cancelled_flag`，这些业务输入字段在 Schedule 中保持不变。每次调用先将请求的 `timer_id` 置为 0，全部校验通过后将计算结果写入请求的 `due_ms`，再直接把调用方的 `timer_ptr` 传给命令队列 Produce，不构造局部计时记录。输入的 `id`、`timer_id` 和 `due_ms` 不作为登记依据，请求记录在调用期间须由当前线程独占。已取消请求直接返回 `ERR_INVALID_PARAM`，不入队也不消耗命令 ID。

ScheduleAt 的截止点来自独立的 `due_ms` 参数，原请求的 `due_ms` 和 `delay_ms` 都不作为输入；`event_queue_ptr`、`event_type` 和 `cancelled_flag` 保持不变。校验失败只清零 `timer_id`，原 `id`、`due_ms` 和 `delay_ms` 不变；入队失败保留已计算的两个时间字段，`id` 不变、`timer_id` 为 0。成功入队后的身份生成、取消和载荷移交与 Schedule 完全相同。

命令 Produce 成功时，队列先将新消息 ID 写回调用方请求的 `id`，再把整条记录按值复制入队；此时队列副本的 `timer_id` 仍为 0。Produce 返回后，两种登记接口均将调用方请求的 `timer_id` 设为 `id`，作为取消凭据。计时线程取出命令后设置副本的 `timer_id = id`，再加入全局堆；到期投递只生成外层 `DominoThreadMessage.id`，载荷内的登记 `id` 和稳定 `timer_id` 均保持不变。重复登记同一记录会覆盖其 `id` 和 `timer_id`，业务仍需使用旧取消凭据时应在下一次登记前保存。

非空请求的校验失败只将 `timer_id` 清为 0，原有 `id` 和 `due_ms` 保持不变。命令入队失败时不消耗消息 ID，调用方的 `id` 保持原值、`timer_id` 为 0，但已经计算并写入的 `due_ms` 保留；命令队列满或内存不足等提交错误直接返回调用方，并终结实例、唤醒等待者。提交成功只表示请求已入队；后台入堆失败时直接发布实例首错并停止推进，不等待诊断输出。

取消不提交命令、不维护有效 ID 集合，也不删除任意堆节点。它按队列指针和 ID 同时匹配：先在命令队列按 `timer.id` 查找，再在全局堆及传入的业务队列按 `timer_id` 线性查找；业务队列扫描先检查 `items_type`，只对 TIMER 消息校验载荷大小并解释 `items_ptr`，跳过其他类型，将匹配请求的标志设为 true。处理时，已取消的命令由计时线程取出后跳过；已取消堆顶直接弹出，尚未成为堆顶的取消项留在堆中；已进入业务队列的取消项由业务消费者取出后跳过。计时线程在批次边界检测到冻结后，命令队列和堆中的取消项保留原位置，直到解冻后由计时线程处理。

命令处理每批只获取和释放一次 `request_mutex`，整批出队与入堆均在锁内完成；堆顶取消检查与投递同样持有请求锁，取消不能在请求迁移中插入。投递时先按值复制堆顶并移除堆顶，再构造独立 timer 载荷和通用消息，调用 `dominoThreadQueueProduce(timer.event_queue_ptr, &message)`。投递失败时先释放尚未转移的载荷，成功后计时线程不再读取或释放它。任意非 `CODE_OK` 的投递结果均直接发布实例首错并停止计时线程，不等待诊断输出；包括队列满、生产端停止、内存分配失败或消息 ID 耗尽，不保留请求等待重试。

取消成功返回 `CODE_OK`；未知、重复取消、队列不匹配或已出队的 ID 返回 `ERR_NOT_FOUND`。扫描遇到 TIMER 类型但空载荷或错误大小时返回 `ERR_INVALID_MESSAGE`，不强制转换畸形载荷。对已投递事件的取消与业务 Consume 在业务队列锁内竞争。业务必须在成功出队后检查 `cancelled_flag`，跳过取消事件；未取消事件一旦出队便取得处理资格，即使尚未开始业务处理，也不能再撤回。业务消费只获取队列锁，不持 timer 请求锁。

业务自行调用 `dominoThreadQueueConsume()`，可以按自身循环选择阻塞或非阻塞模式，依据对象存在性、任务状态和当前 `timer_id` 判断通知是否仍有效。消费或业务处理不需要通知 timer，也不受 timer 的分派函数约束。ID 仅在本次 Init 生命周期内有效；复用业务队列进入下一次 timer Init 前，必须排空旧事件，避免与重新生成的 ID 混淆。

以下调用片段展示业务队列的初始化、登记、消费和取消；业务模块应处理各步返回码。控制线程先完成 time 初始化，再调用 `dominoTimerModuleInit()`。

```c
static DominoThreadQueue business_inbox;

DOMINO_CODE code = dominoThreadQueueInit(&business_inbox, sizeof(DominoThreadMessage), DOMINO_ENGINE_QUEUE_DEFAULT_CAPACITY,
                                       DOMINO_ENGINE_QUEUE_MAX_CAPACITY);
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
    if (message.items_type == DOMINO_THREAD_MESSAGE_TIMER) {
        if (message.items_ptr == nullptr || message.items_size != sizeof(DominoTimer)) {
            code = ERR_INVALID_MESSAGE;
        } else {
            const DominoTimer* timer_ptr = message.items_ptr;
            if (!timer_ptr->cancelled_flag) {
                // 按 timer_id、event_type 及当前业务状态处理到期通知。
            }
        }
    } else {
        // 按 items_type 分派 AI 结果、请求或其他命令，并校验对应载荷大小。
    }
    dominoThreadMessageRelease(&message);
}
```

```c
// 取消时传入登记所用的同一业务队列。
DOMINO_CODE code = dominoTimerCancel(timer.timer_id, &business_inbox);
```

## 调度和等待

全局堆按 `(due_ms, timer_id)` 排序，无独立排序序号或 ID 索引。入堆、弹出堆顶为 O(log M)，偶尔扩容需要 O(M)，M 是全局堆中的请求数量；取消查找为 O(N)，N 是命令、堆和指定业务队列中的请求总量。已入堆的请求统一按到期时间选择投递，同刻事件按登记 ID 排序；尚在命令队列中的请求不参与堆排序，各业务线程的实际消费顺序也不由 timer 控制。

堆数组从索引 1 开始存储，`events_ptr[0]` 保留不使用，有效元素范围为 `[1, count]`，堆顶位于索引 1。`capacity` 表示可存储的元素数量，实际分配 `capacity + 1` 个数组槽位；索引 `i` 的父节点为 `i / 2`，左右子节点分别为 `2 * i` 和 `2 * i + 1`。

每轮先检查引擎全局退出和 timer 本地退出，再检查 time 冻结。通过检查后先处理最多 64 个堆项，再处理最多 64 条登记命令，共同构成一批，优先投递已经入堆的到期事件。冻结或普通退出发生在批次内部时，当前批次完整执行，下一轮才停止；实例故障仍在项间检查并立即终结当前批次。取消项的清理也占处理预算，不按业务队列轮转。登记批次输出实际出队条数，包括取消项；入堆失败立即终结实例，不处理剩余命令。本轮只要处理过登记命令或耗尽堆处理预算，就立即进入下一轮，否则根据当前堆顶、累计现实时间和唤醒状态决定是否等待。

`request_mutex` 保护全局堆、请求迁移、取消和 `wake_pending_flag`，并作为 `wake_condition` 的等待互斥锁。timer 本地退出请求 `stop_requested_flag`、线程停止回执 `stopped_flag` 和运行首错 `failure_code` 是原子变量；引擎退出由 `dominoEngineIsStopRequested()` 读取。不再维护冻结确认标志、冻结条件变量或冻结回执接口。需要同时持锁时，顺序固定为请求锁后队列锁；业务直接消费只持业务队列锁，不获取请求锁。

Schedule 和 ScheduleAt 先执行命令队列的非阻塞 Produce，队列内部锁释放后，入队成功才取得 `request_mutex`，设置唤醒标志并发出信号；不能持有队列锁再获取请求锁。非阻塞入队只表示不等待队列腾出空间，入队后的通知可能等待正在进行的命令批次、堆处理或取消扫描，但不等待该请求入堆或到期。Cancel、时间通知和 Exit 同样可能短暂等待当前持锁操作结束，但时间通知不等待线程冻结或恢复回执。

登记成功、取消成功和解冻通知会在请求锁内将 `wake_pending_flag` 置为 true 并唤醒计时线程；普通冻结不通知计时线程。Cancel 成功后直接使用已经持有的请求锁通知。引擎退出先发布全局退出标志，再通过相同通知入口唤醒计时线程。单独调用 timer Exit 时，持请求锁发布本地退出标志后唤醒。Exit 解锁后短暂睡眠等待停止完成回执，读取本次停止结果，成功后才 join；等待期间发现实例首错立即返回，不继续清理。

计时线程在每轮检查工作前持请求锁清除唤醒标志，发现冻结时在同一锁上条件等待；等待谓词同时检查全局退出和本地退出，因此暂停期间退出不需要解冻业务时钟。登记或取消通知只使线程重新检查冻结及退出状态，仍冻结则继续等待，不检查堆顶或取消标记。工作等待也在请求锁内检查退出、冻结和唤醒标志；期间收到通知就返回主循环，避免通知恰好发生在睡眠前而被漏掉。没有待处理通知时，只读取全局堆顶，不检查业务队列容量。堆顶已取消或已经到期就跳过等待；堆空则条件等待；有未来堆顶则按剩余现实毫秒直接计算等待间隔，使用标准 `cnd_timedwait()`。`cnd_wait()` 和 `cnd_timedwait()` 原子释放 `request_mutex`，唤醒后重新取得该锁。醒来后返回循环头部先检查退出，再检查冻结。UTC 系统时钟调整可能改变一次等待的长度，到期判断仍以 time 模块累计未冻结的现实时间为准。

## 冻结和退出

普通冻结由客户端直接调用 time 固定业务时钟，不向主循环发控制请求、不通知计时线程、不等待冻结回执。计时线程只在批次边界观察冻结，当前批次可以继续迁移命令、回收堆项和投递；随后等待解冻或退出。原本等待命令或计时的线程保持等待，到期或收到其他通知时重新检查冻结，无须为暂停单独唤醒。冻结成功只表示业务时钟停止，不表示业务数据或队列已经停止变化。日志模块和消息队列本身不受冻结影响。

Schedule、ScheduleAt 和 Cancel 在冻结期间仍允许提交及标记请求，引擎发布普通退出后也允许尚未完成的业务批次继续调用，直到调用方结束。尚未处理的命令、取消项和已投递事件保留；timer 不维护另一套暂停开关，也不等待业务处理结束。

time 初始化完成后才可启动 timer。客户端在冻结时钟下先启动 human 执行／规划线程，再直接调用 `dominoTimerModuleInit()`，最后创建主循环；主循环不负责启动其他线程。成功后客户端直接解冻 time，再调用 `dominoTimerNotifyTimeChanged()`，让线程重新检查时钟和等待截止点；它只通知，不修改 time 或平移已有截止点。启动失败由客户端直接通知已创建业务线程停止，保留已有资源。普通恢复继续处理已有请求，不需要清空重建计时器或全员重新登记。首次启动和加载存档的调度初始化由业务模块单独负责。

生命周期接口由同一客户端串行调用；普通冻结、解冻及退出停钟均由客户端直接写入 time，保证这些时间写入不并发。实例失败时，`dominoTimerRequestFailure()` 记录首错、请求线程停止并广播工作条件，不 join 或释放资源；初始化尚未完成或正常退出后不会访问同步对象。后台失败通过实例首错和退出回执传播。该接口不得与 Init/Exit 并发。正常退出顺序：

1. 客户端先发布全局退出标志、冻结并校验业务时刻，再直接唤醒主循环、业务线程和 timer，无需解冻。各线程先检查退出再检查暂停；已经开始的批次完成后退出。此时 timer 命令队列、计时堆和业务事件队列全部保活，允许尚未结束的业务批次登记、取消及投递。
2. 客户端先 join 主循环，再停止并 join human 执行和规划线程，结束所有 Schedule、ScheduleAt 和 Cancel 调用；业务收件队列及其生产端继续保留。随后调用 timer Exit，发布本地退出、唤醒并等待计时线程完成，成功后 join，再释放 timer 命令队列、堆和同步资源。Exit 不与其他 timer API 并发。
3. 客户端确认写入线程全部结束后，统一将人物结算到固定时刻，再清理剩余消息并释放载荷、销毁业务队列。队列 Destroy 不会释放消息 `items_ptr` 指向的块。计时线程 join 且业务消费者全部结束后，才可搬移、销毁或重新初始化业务队列；下一次 timer Init 前须排空旧事件。
4. 客户端完成运行资源释放后，准备导航并保存世界，再释放世界资源和关闭日志。主循环不负责退出停钟或协调收尾；普通暂停不负责全员结算或提供可保存快照。

Exit 只丢弃尚未投递的命令和堆项，不排空、不停止、不销毁业务队列。客户端 Init/Exit 自身资源步骤失败后立即返回本次错误，不继续后续步骤，由上层传播至宿主结束进程。计时线程自身同步原语失效统一沿用原有终止策略，包括退出阶段；不在等待或解锁失败后带锁发布退出回执，避免客户端和其他业务线程永远阻塞在该锁上。业务时钟、入堆或投递等错误仍通过实例首错及线程退出结果传播。

## 验证范围

当前说明基于源码合同，尚无性能实测，也未验证引擎整体初始化、冻结恢复及退出接入。源码或格式检查不代表线程竞态、运行行为或容量已经验证。
