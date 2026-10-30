# shared 公共头文件说明

`shared/include` 只放跨模块可见的类型、错误码、通用工具及日志／队列接口。实体数组、ID map、模块生命周期函数等运行时状态由 `engine/src/*/entry.h` 暴露给 engine 内部使用，不属于 shared 的职责。

## 文件职责

| 文件 | 说明 |
|------|------|
| `domino_shared_types.h` | 基础 ID、名称/描述、坐标类型，32 位现实毫秒类型 `domino_runtime_ms_t`，以及核心世界设计约定。 |
| `domino_shared_error_codes.h` | 统一 `DOMINO_CODE` 错误码和错误码字符串转换。 |
| `domino_shared_lifecycle.h` | shared logger 使用的未初始化、已初始化、已启动、已停止状态枚举；引擎内部模块不据此重复维护阶段。 |
| `domino_shared_logger.h` | 多实例异步日志及满队列等待。 |
| `domino_shared_queue.h` | 通用 FIFO、非阻塞／阻塞入队、批量消费、控制唤醒和版本变化等待；不依赖业务消息定义。 |
| `domino_shared_common.h` | 直接打印消息、打印致命错误并 exit 的公共方法，以及 Linux 当前实现下的时间、路径和文件工具。 |
| `domino_shared_host.h` | 地域、岛屿、运动物体、全局自增 ID 结构。 |
| `domino_shared_human.h` | 64 字节人物核心实体 `DominoHuman` 和 4096 字节扩展属性 `DominoHumanData`。 |
| `domino_shared_social.h` | 组织、建筑、城市、国家、资产及其扩展 data。 |
| `domino_shared_behavior.h` | 分离的任务／行为类型与实例；`DominoTask` 同时保存业务事实、独占行为块指针和运行期执行版本。 |
| `domino_shared_nav.h` | fork road、road、road line 和导航边权类型。 |
| `domino_shared_core.h` | 核心公共聚合头占位，当前未导出内容。 |

## 公共数据约定

- 业务时刻和时长固定使用 `domino_runtime_ms_t`（`uint32_t`）累计未冻结现实毫秒；0 和 `UINT32_MAX` 均合法，越界报错。底层 64 位纳秒、UTC 墙上时间戳和 ID 不是业务毫秒类型；不能由它们推断需要扩宽业务计时。
- `0` ID 保留为无效值。全局自增计数器在 storage 根 `meta.json` 中持久化。任务与人物等实体统一在 `DominoGlobalIncrementID` 中维护；`task_increment_id` 是 32 位原子计数器，表示下一候选 ID，0 是合法初值或回绕值。任务分配复用 `DOMINO_ALLOC_NON_ZERO_ID` 跳过 0，再由任务池单写线程通过现有 ID map 跳过仍占用的 ID；失败取号留下的空洞不回退，ID 大小不表示任务先后。
- 实体数据以扁平数组保存，engine 内部再用 `khashl` 建立 `id -> array index` 映射；shared 只定义实体布局，不保存全局数组或映射。
- 名称和描述使用 `domino_name_id_t`、`domino_description_id_t` 引用运行时字符串表。storage 落盘时保存字符串本身，加载后重新分配运行时 ID，因此名称/描述 ID 不保证跨 save/load 稳定。
- `other_info` 字段是结构体对齐、预留或运行时压缩信息空间。当前 storage serde 通常不读写这些字段，新增持久化字段时需要同步更新对应 `serde_*.c` 和 `engine/src/storage/readme.md`。
- `DominoHuman` 固定为 64 字节，保存必需的 data 指针、`human_id`、状态、运动对象引用或静态坐标、地域／空间，以及 `active_plan_request_id`、`wakeup_game_timer_id`、`wakeup_due_ms` 和规划待投、执行就绪两个调度标志。请求与计时器 ID 保持 64 位；`wakeup_due_ms` 使用 `domino_runtime_ms_t` 保存当前登记的绝对截止点，仅在 `wakeup_game_timer_id` 非零时有效，用于同一截止点去重，不进入存档。到期消息以 `data_id = human_id` 定位人物，并匹配当前非零 `wakeup_game_timer_id`，无需额外唤醒版本或关联表。`DominoHumanData` 固定为 4096 字节，以相同 `human_id` 标识，人物与 data 数组同序共用一个人物 ID 索引；不再保留独立 data ID、AI 容器或 AI 索引。data 前 40 字节集中放置随机状态、人物 ID、三个活动槽、日程版本、结算时刻、六项需求及健康／生命值，当前位置、导航地址、财富与调度补充字段紧接其后；128 字节的持有任务 ID 数组排在这些高频字段之后，从预留空间中调整且不扩大结构；其余属性按身体外观、组织职业、身份、家庭社交、资产、能力性格和位置历史组织。字段采用自然对齐，`other_info` 补齐预留空间，不使用 packed 或位域压缩。
- 其他核心实体如 `DominoOrg`、`DominoBuilding`、`DominoCity`、`DominoCountry`、`DominoAsset`、`DominoRoad`、`DominoRoadLine` 为 128 字节；扩展 data 多为 4096 字节。
- 任务和行为拥有独立类型与状态；`DominoTask.type` 使用 `DOMINO_TASK_TYPE`，`DominoBehavior.type` 使用 `DOMINO_BEHAVIOR_TYPE`，对应类型查询接口也使用各自枚举。存档仍写枚举数值，加载按字段类型解码可信状态。engine 的全局任务池直接保存 `DominoTask`，由任务 ID map 定位；任务通过 `behaviors_ptr` 独占连续行为块，并保存自己的执行版本、行为游标及计划／实际时间，行为保存各自进度。`execution_version` 保持 `uint64_t`，新建和加载导入时初始化为 1，仅表示任务自身版本，不要求跨任务唯一。后续执行线程在需要使旧结果失效时，检查 `UINT64_MAX` 后直接执行 `task->execution_version++`；达到上限则返回 `ERR_OUT_OF_RANGE` 且版本不变。执行版本不持久化；单行为睡眠执行器和规划结果消费已经接入。结构体拷贝不会复制指针指向的行为数组，跨线程快照须独立复制所需行为内容；不再有全局行为索引或人物内联任务。
- `DominoHumanData` 用 `task_id_list[DOMINO_HUMAN_TASK_COUNT_MAX]` 与 `uint8_t task_count` 保存本人全部持有任务，并另存固定三个活动槽位（主通道、次通道 1、次通道 2）。持有数组最多 32 项，包含待启动、运行及未回收终态；添加时追加，删除后稳定前移，状态及计划时间变化不重排。任务是相互独立的扁平记录。任务实例的 `channel_index` 保存所属通道并参与存档；三个通道均可承载任意已定义类型，主通道优先于两个同级次通道。槽位允许为 0，但非零任务必须归属该人物并处于对应通道的运行状态。六项需求 `energy/food/water/hygiene/recreation/social` 复用原字段，以 `uint8_t` 保存 0～100 整数分，`DOMINO_HUMAN_NEED_MAX=100`；不另存小数余量，规则比较与显示直接使用整数分。自然变化约定按完整未冻结现实分钟结算，`needs_updated_at_ms` 仍为累计业务毫秒，未满分钟的时间留待下次，跨多分钟补结算；这不改变任务、行为和 game_timer 的毫秒精度，需求结算入口已经实现。
- 存档版本 21 中，`human.entities[].data` 保存人物属性、日程和需求结算事实；必需的 `human_ai` 模块按 128 人分片，每条记录仅含 `human_id` 与嵌套的任务／行为。根 meta 的 `global_increment_id.task` 保存下一候选任务 ID，不要求大于已保存任务 ID。人物核心中的调度标识、data 中的 `pending_plan_reasons`、任务的 `behaviors_ptr/execution_version` 均不进入存档；加载重建行为块并将每个任务的执行版本初始化为 1，之后清空人物调度运行态时不修改任务版本。JSON `task_id_list` 仅保存有效前缀，加载由长度恢复 `task_count` 并保留原顺序，嵌套任务按同样的 ID 顺序保存，加载直接导入可信任务，人物数组是提交顺序的唯一来源。执行器后续按通道、计划开始时间及启动条件筛选，同时间以数组位置决定先后，不依赖任务 ID 数值。已定义的任务／行为类型统一保存和加载，任务持有非空行为序列。行为状态、实际时间和有效进度直接保存为行为对象的字段；行为保留 `started_at_ms`、`ended_at_ms`、`accounted_at_ms`、`active_elapsed_ms` 与 `started_flag`，不预留目标建筑、等待检查时刻或一次性效果提交标志；睡眠目标有效时长由行为的 `planned_end_ms - planned_begin_ms` 推导，首次启动时修订计划区间，执行期间保持长度不变；存档直接保存计划区间与 `active_elapsed_ms`，剩余有效时长由两者相减得到；目前支持单行为睡眠的自主规划与执行，其他任务的规划和执行仍待实现。

人物状态由 `DOMINO_HUMAN_STATUS` 定义：`NORMAL=0`、`EXCEPTION=1`，复用核心记录的 `status` 字段持久化，不改变 64 字节布局。任务仅有 PLANNED、RUNNING、SUCCEEDED、FAILED、CANCELLED；失败或取消立即使人物异常，执行器停止该人物的任务推进、需求结算和自主规划。异常状态保存重载后仍保留，没有任务暂停、恢复或父子层级。游戏全局冻结／解冻与正常任务的存档续跑继续保留。

## 当前实现边界

- 当前跨平台封装仍以 Linux 实现为主，`domino_shared_common.h` 中的路径、rename、fsync、递归删除等函数未完整覆盖 Windows/macOS 差异。
- `dominoWallTimeMs()` 与 `dominoMonotonicTimeNs()` 直接返回采样值，采样失败直接打印 stderr 并 exit，不通过结果码传播；它们分别用于存档命名和 time 内部采样，不能代替 32 位业务毫秒接口。
- `domino_shared_nav.h` 中 `dominoAddRoad()` 与 `dominoRemoveRoad()` 已在 engine nav 模块实现；`dominoUpdateRoadWeight()` 与 `dominoBlockRoad()` 当前只是预留声明。
- 物理、个体智能、群体智能和超级智能阶段仍是模拟流水线的设计目标；当前运行逻辑由消息消费主循环和 human 行为规划工作线程承载。
- 任务模型和最小 AI 存档已接入；当前受限睡眠执行器已接通自主睡眠、计时唤醒及存档续接，普通暂停只在批次边界等待，退出在线程结束后统一结算；通用需求决策和三通道执行仍待实现。

## 通用工具边界

`domino_shared_common.h` 的路径、文件、时间格式化和诊断函数可独立调用，按各自头文件契约处理输入、输出和失败结果，不依赖引擎生命周期或全局初始化状态。内部操作直接使用前序校验建立的条件，不重复检查非空路径、分隔符等不变量；实际采样、格式化、容量、分配和 IO 失败仍须处理。

`dominoReadFileBytes()` 在两个输出指针均有效时先将结果置为 nullptr／0，空文件成功返回这组值；文件 IO 失败仍保留这组输出并清理本次资源。`dominoPrintMessageAt()` 保留空格式串无操作、缺失来源输出问号的约定。调用方按业务语义处理通用工具的可恢复结果，工具不逐层重复打印。

`dominoRemovePathRecursive()` 先忽略非根路径的末尾斜杠，再复用 `lstat()` 的类型结果：非目录（包括根符号链接）直接删除，真实目录才交给 `nftw` 后序遍历；目录内的符号链接也只删除链接本身，不进入目标。没有末尾斜杠的路径直接交给内部删除步骤，不复制路径；全部由斜杠组成的根路径保持原样。

## 多实例日志

`DominoLogger` 的存储由调用方提供。engine、client-hub、engine-remote/local 和 engine-remote/server 各自持有实例；每个实例独立配置队列容量、默认级别和可选默认日志文件，模块可通过 `DominoLogModule.file_path` 指定自己的文件。实例统一管理 writer、模块表、文件和 seq。

调用方通过自己的可变参数包装函数将参数传给 `dominoLogVa`，包装函数负责配对调用 `va_start` 和 `va_end`。

```c
#include <stdarg.h>

#include "domino_shared_logger.h"

static void logServiceMessage(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc,
                              const char* fmt, ...) DOMINO_LOG_PRINTF_ATTR(5, 6);

/** @brief 调用方的日志包装示例，将可变参数转发给 shared 日志服务。 */
static void logServiceMessage(DominoLogger* logger_ptr, domino_log_module_id_t module_id, DOMINO_LOG_LEVEL level, DominoLogSourceLocation loc,
                              const char* fmt, ...) {
    va_list arg_list;
    va_start(arg_list, fmt);
    dominoLogVa(logger_ptr, module_id, level, loc, fmt, arg_list);
    va_end(arg_list);
}

void logWithTwoServices(void) {
    DominoLogger first_logger = {0};
    DominoLogger second_logger = {0};
    const DominoLogModule first_modules[] = {
        {.module_id = 0U, .name = "core"},
        {.module_id = 1U, .name = "nav", .file_path = "first-nav.log"},
    };
    const DominoLogModule second_modules[] = {{.module_id = 0U, .name = "core"}};
    dominoLogInit((DominoLogConfig){.file_path = "first.log"}, first_modules, sizeof(first_modules) / sizeof(first_modules[0]), &first_logger);
    dominoLogInit((DominoLogConfig){.file_path = "second.log"}, second_modules, sizeof(second_modules) / sizeof(second_modules[0]), &second_logger);
    dominoLogStart(&first_logger);
    dominoLogStart(&second_logger);

    const DominoLogSourceLocation loc = {.file = __FILE__, .line = __LINE__, .func = __func__};
    logServiceMessage(&first_logger, 0U, DOMINO_LOG_LEVEL_INFO, loc, "%s", "first service");
    logServiceMessage(&first_logger, 1U, DOMINO_LOG_LEVEL_ERROR, loc, "%s", "nonfatal navigation error");
    dominoLogStop(&first_logger);
    dominoLogDestroy(&first_logger);
    logServiceMessage(&second_logger, 0U, DOMINO_LOG_LEVEL_INFO, loc, "%s", "second service continues");
    dominoLogStop(&second_logger);
    dominoLogDestroy(&second_logger);
}
```

- 初始化先校验完整模块列表，再分配队列、以追加模式打开配置的文件并解析模块状态，不启动 writer；返回后不借用模块数组或文件路径。Start 单独创建 writer，成功后才允许记录日志。首次对象必须零初始化（静态对象、`={0}` 或 `calloc`）；Init 仅接受未初始化状态，重复调用在创建资源前直接致命退出。Init 基于空对象创建资源，不再重复整体清零；Destroy 清空对象，完成后可直接再次 Init。
- 模块 ID 和名称在实例内必须唯一；名称必须非空且在固定数组内以 NUL 结尾。`module_count = 0` 时允许空列表；模块 `min_level = 0` 在初始化时解析为配置的默认等级，默认等级只用于本次 Init，实例只保存解析后的模块等级，显式等级独立生效。
- 每条日志只写一个目标：模块 `file_path` 优先，其次实例 `DominoLogConfig.file_path`，两者都未配置时写 stdout；nullptr 和空字符串都表示未配置。所有等级（包括非致命 ERROR）遵循同一规则，日志内容不写 stderr，也不同时复制到默认文件或控制台。示例中 core 写 `first.log`，nav 写 `first-nav.log`。
- 实例内直接复用 `DominoLogModule`，初始化时由 logger 维护其 `output_ptr`，writer 通过队列元素的 `module_index` 取用。调用方无需设置 `output_ptr`，传入值会被忽略；实例副本不保留配置路径，`file_path` 保持 nullptr。文件由 logger 统一打开和关闭；指定的文件打开、写入、刷新、fsync 或关闭失败时直接走公共致命退出路径。
- `buffer_size = 0` 使用默认容量 10240；容量不得超过 `UINT32_MAX`，保留分配大小溢出检查。日志队列固定容量，满载时本次日志调用向所属模块的同一输出流同步提示一次并刷新，然后用 `dominoThreadQueueProduce(..., true)` 等待直到入队，不丢日志、不定时轮询。提示不再次入队；writer 消费后通过 `not_full` 唤醒等待者。
- 致命错误统一使用 `DOMINO_EXIT_WITH_ERROR(code, fmt, ...)`：宏捕获调用位置，`common.c` 的 `dominoExitWithErrorAt()` 直接打印 stderr，然后执行 `exit(EXIT_FAILURE)`，允许退出回调和标准 I/O 刷新。诊断不进入日志队列，诊断本身失败不递归报错。
- 运行阶段非致命错误和正常日志进入异步日志；初始化、启动、停止、销毁及冻结／解冻操作的非致命诊断统一使用 `DOMINO_PRINT_MESSAGE(fmt, ...)` 直接写 stdout。宏捕获调用位置，`common.c` 的 `dominoPrintMessageAt()` 打印来源和完整正文。共用业务工具只返回可恢复结果，由掌握业务语义的调用边界选择输出；日志层不维护阶段标志或进行阶段分流。直接输出不截断、不入队、不退出，输出失败也不递归报错。日志等级只表达严重程度，不决定是否终止；ERROR 日志同样可以是可恢复的业务失败。
- 日志不再维护故障回调、首错状态或错误查询接口；`Init`／`Start`／`Stop`／`Destroy` 返回 void。致命错误会结束整个宿主进程，队列中的剩余日志不保证写出。
- 同一实例支持多个生产者；初始化后模块表只读。对象地址和存储必须保持有效，直至销毁返回，期间不得复制、移动或直接修改成员。
- `dominoLogVa()` 统一完成空指针、格式指针和等级过滤，模块查找辅助函数只按模块 ID 和最小等级匹配。只有通过过滤的日志才检查 STARTED 状态；内部格式化、入队和 writer 使用已确定的模块下标，不重复校验入口条件。日志四阶段状态、序号、线程标识及截断信息仍各自承担生命周期或输出职责。
- 每条通过过滤和生命周期检查的日志，由业务调用线程在整理来源、正文和等待入队前采样 UTC 时间，保存为 `YYYY-MM-DDTHH:MM:SS.mmmZ` 并随队列记录按值传递；writer 使用该时间戳，不按批次重新采样。满载等待和延迟写出不改变调用时间戳。UTC 墙上时间受系统校时影响，不用于计算累计未冻结业务时长或保证日志顺序。
- 生命周期由调用方串行管理为 Init → Start → Stop → Destroy；也支持 Init → Stop → Destroy。Stop 前须停止并等待所有生产者；Stop 调用 `dominoThreadQueueBeginFlush()` 拒绝新消息，已 Start 时唤醒 writer 并 join 等待排空，仅 Init 时跳过 join；随后对各模块文件及实例默认文件完成 fflush 和 fsync，被使用的 stdout 仅 fflush，保留队列和文件。两条生命周期路径正常返回均承诺完成文件同步，继承的默认文件只同步一次；fflush 或 fsync 失败直接致命退出。每次 Init 后至多 Start 一次并调用一次 Stop；Stop 后不支持重新 Start，必须 Destroy 后重新 Init。实例使用 `DominoModuleLifecycleState` 区分未初始化、已初始化、已启动、已停止；有效日志在未 Start 或 Stop 后写入直接致命退出，不会入队或等待。空实例、非法等级和未匹配／被等级过滤的模块继续丢弃；其他实例继续正常运行。
- Destroy 要求 Stop 已完成，只关闭各模块文件和实例默认文件、销毁队列并清空对象，不内部停止或 join writer。继承的默认文件只关闭一次；stdout 不关闭、不承诺 fsync，日志服务不刷新 stderr。销毁后可在原地址重新初始化；空指针、未初始化、重复 Stop／Destroy、未 Stop 就 Destroy 和 Stop 后 Start 均直接输出 stderr 并退出。
- stdout 由进程共享，实例之间没有全局输出顺序；文件输出需要隔离时为各实例和模块设置不同路径。tid 是进程内分配并在线程局部缓存的数字标识。

## 通用线程队列

接口和生命周期约定见 `shared/include/domino_shared_queue.h`。首次队列对象须零初始化，Init 拒绝已初始化对象；Destroy 拒绝空指针、未初始化和重复销毁。队列没有自己的线程，全部使用者停止访问后即可 Init → Destroy，无需虚构 Start／Stop。BeginFlush、WakeAll 仍保留各自的通知与排空语义。队列按固定元素大小复制数据，只维护首字段 ID；
engine 业务消息定义保留在 `engine/src/queue/thread_message.h`，调用方按需显式包含，日志不依赖这些业务类型。
`domino-shared` 导出标准线程链接依赖，macOS 的 TinyCThread 依赖也由 shared 统一传递。

队列通过 `initialized` 明确表示实例是否已初始化，供公共入口在加锁前检查；Init 完整建立资源后置为 true，Destroy 清空对象时复位为 false。`item_size` 只表示元素字节数，`items_ptr` 会随扩容变化，均不承担初始化状态判断。Init 使用调用方的零对象或 Destroy 已清空的对象，不重复整体清零。Produce 通过第三个参数 `blocking` 选择是否等待空位，两种模式共用一处参数与初始化校验，Consume 复用 ConsumeBatch；不为内部等待、扩容或复制辅助步骤重复添加边界检查。条件等待会释放锁，唤醒后仍须重新检查 `flushing_flag`、元素数、容量或 `wake_epoch`；两个等待者计数共同用于区分等待类型并选择通知；`waiter_count` 同时用于销毁前检查是否仍有等待者，均须保留。

队列用一个 `flushing_flag` 表示排空状态。`dominoThreadQueueBeginFlush()` 在锁内设置该状态、推进唤醒版本并通知全部等待者；进入后不提供恢复，销毁并重新 Init 才可再次生产。
排空状态下，Produce 的两种模式均拒绝新消息并返回 `ERR_QUEUE_FLUSHING`；Consume 和 ConsumeBatch 继续返回已有消息，排空后才返回此结果；Wait 立即返回此结果，不保证队列已空。BeginFlush 不消费消息或等待线程退出，调用方仍负责载荷释放、线程 join 和销毁顺序。
队列 `Init`、`BeginFlush`、`Destroy` 返回 void；任何阶段的分配、同步、ID 耗尽故障都直接打印 stderr 并 exit。
参数错误、非阻塞满队列、空队列和排空状态仍是调用方处理的结果；队列不自行决定业务日志等级。
运行期间通过 `WakeAll` 通知业务状态变化，不改变 `flushing_flag`；唤醒版本、条件等待和 join 协议继续承担正常生命周期同步。
