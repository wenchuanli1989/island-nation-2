# engine 公共头文件说明

`engine/include` 提供引擎生命周期、消息通信和多实例日志的公共 API。日志实现位于 `engine/src/logger/log.c`，engine、client-hub 和 engine-remote 可各自持有独立实例。

## 文件职责

| 文件 | 说明 |
|------|------|
| `domino_engine.h` | 引擎初始化、运行、退出、生命周期错误查询、时间冻结/解冻及消息收发接口。 |
| `domino_engine_log.h` | 多实例异步日志服务 API；每个实例有独立队列和 writer 线程。 |

## 引擎生命周期

生命周期和睡眠修改接口由 Init 成功登记的客户端线程串行调用。普通冻结、解冻及退出停钟均由客户端直接写入 time，不经过主循环转发，因此这些时间写入不会并发。正常顺序为 `Init（冻结）→ AuthorizeSleep／SubmitSleep → Run → Freeze／Thaw → Exit`；仅 Init、未 Run 时也可直接 Exit。

| 接口 | 成功与失败边界 |
|------|------|
| `dominoEngineInit()` | 初始化模块、加载并校验世界，保持时间冻结。 |
| `dominoEngineRun()` | 客户端准备主循环队列和运行通知，在冻结时钟下直接调用 human Start 完成启动校验并建立执行／规划线程，再启动 timer，最后创建主循环。成功后进入 `RUNNING`，直接解冻时钟并唤醒主循环和业务线程；主循环不承担启动协调或发布启动回执。首次启动分批扫描人物建立调度，就绪不表示全部人物已经登记 timer、完成规划或启动任务；启动或解冻失败由客户端通知已创建业务线程停止，终结本次实例。 |
| `dominoEngineFreezeTime()` | 客户端直接调用 time 冻结时钟，不向主循环发控制请求，不通知业务线程、不等回执。业务线程只在批次入口检查冻结，当前有限批次完整执行；原本等待中的线程保持等待。普通冻结不统一结算人物，也不保证可立即保存；冻结后的时刻范围校验失败终结实例。 |
| `dominoEngineThawTime()` | 客户端直接解冻时钟，再通知主循环、executor、planner 和 timer，不向主循环发控制请求，也不等待线程恢复回执。恢复续接留队消息和已有调度，不做全员扫描；时钟切换失败记录首错并终结实例。 |
| `dominoEngineExit()` | 客户端将运行世界置为 `STOPPING`，发布退出标志、冻结并校验业务时刻，再直接唤醒主循环及业务线程，暂停状态无需解冻。客户端依次 join 主循环、human 执行／规划线程和 timer，再统一结算人物、清理残留队列及运行资源，准备导航、保存并释放世界，最后关闭日志。主循环只结束当前批次并退出，不协调其他线程收尾。所有投递者结束前保持目标队列有效；全部成功后回到未初始化状态，可在同一进程 Init 新世界。 |
| `dominoEngineGetLifecycleError()` | 读取本次实例首个终止错误，包括 Init／Run／Exit、Freeze／Thaw 以及队列满和后台运行故障；未失败时返回 `CODE_OK`。查询不清理资源、不改变状态。 |

Init／Run／Exit、Freeze／Thaw 的状态、参数校验或执行步骤遇到首个错误即终结本次实例，包括 human Start 的冻结世界和授权校验失败。所有线程队列入队均不等待容量，达到最大容量立即返回错误；调用方保有未移交载荷，并将队列及后台运行故障发布到独立的实例错误状态。失败通知不经业务队列，启动、停止的完成等待和命令结果等待也会检查并取得错误。不执行失败清理、回滚、重试，也不由引擎主动调用 `abort()` 或 `exit()`。`FAILED` 后唯一允许调用的引擎接口是 `dominoEngineGetLifecycleError()`；宿主报告错误并结束进程，在此之前残留线程可能访问的资源须保持有效，失败实例不可重用，不保证违规重复调用其他 API 会返回已记录错误。保存错误同样遵循这一边界；磁盘事务仍由后续新进程加载时恢复，不能在失败进程中重试 Exit。

Init 成功且尚未 Run 时可配置睡眠授权、提交待执行睡眠；运行后的暂停状态仍拒绝同步睡眠授权和提交，避免等待暂停中的业务消费者。`dominoEnginePushMsg()`／`dominoEnginePopMsg()` 和底层消息队列操作不受冻结影响；消息可留队，阻塞取消息仍可能等到业务线程恢复。日志也持续工作。业务线程每轮先检查退出、再检查冻结，暂停等待可由退出或失败通知解除；当前批次的状态修改、规划请求、规划回传和 timer 登记完整执行，下一轮才等待。

上述传播约定适用于可返回的业务和运行错误；底层同步原语损坏及 `lib/` 库内部仍保留既有 `abort()` 终止策略，该情况不会转换为业务错误传播。

Init／Exit 调用链直接上抛原错误，由宿主报告，不通过运行期日志队列记录生命周期诊断。logger 的关闭排空错误也由 Destroy 返回，日志入队满载同样立即失败，不等待空位。

## 多实例日志

`DominoLogger` 的完整结构体定义由公共头文件提供，实例存储由调用方分配。engine、client-hub、engine-remote/local 和 engine-remote/server 分别持有自己的实例；也可使用局部、静态或调用方分配的存储初始化更多实例。每个实例独立配置队列容量、默认级别和可选日志文件，独立管理 writer、模块注册表和 seq。

```c
#include "domino_engine_log.h"

/* 失败返回后实例不可重用；存储保活，由宿主决定何时结束进程。 */
static DominoLogger first_logger;
static DominoLogger second_logger;

int logWithTwoServices(void) {
    const DominoLogModule modules[] = {{.module_id = 0U, .name = "core"}};
    const size_t module_count = sizeof(modules) / sizeof(modules[0]);
    int result = dominoLogInit((DominoLogConfig){.file_path = "first.log"}, modules, module_count, &first_logger);
    if (result != 0) {
        return result;
    }
    result = dominoLogInit((DominoLogConfig){.file_path = "second.log"}, modules, module_count, &second_logger);
    if (result != 0) {
        return result;
    }

    const DominoLogSourceLocation loc = {.file = __FILE__, .line = __LINE__, .func = __func__};
    dominoLogMsg(&first_logger, 0U, DOMINO_LOG_LEVEL_INFO, loc, "first service");
    result = dominoLogDestroy(&first_logger);
    if (result != 0) {
        return result;
    }
    dominoLogMsg(&second_logger, 0U, DOMINO_LOG_LEVEL_INFO, loc, "second service continues");
    return dominoLogDestroy(&second_logger);
}
```

- `dominoLogInit(config, modules_ptr, module_count, &logger)` 在初始化阶段一次性校验并复制完整模块列表，再分配内部资源并启动 writer；返回后不借用传入模块数组。存储无需预先清零，初始化失败立即返回并保留已取得的资源，不清理、不回滚、不重试。失败对象不得继续使用、重新初始化或作为正常对象销毁；已成功初始化且未成功销毁的实例不得再次初始化。
- 每个实例通过 `queue_ptr` 持有独立的 `DominoThreadQueue`，队列存储与同步对象由 queue 模块管理。`buffer_size = 0` 使用默认容量 10240；容量必须不超过 `UINT32_MAX`，且分配大小不得溢出。队列容量固定，满载时立即保存日志实例首错并上报引擎实例错误，停止后续入队，不等待空位或重试；各日志实例均遵守这一规则。
- 调用方提供的存储须一直有效，直到 `dominoLogDestroy(&logger)` 成功返回；实例必须保持固定地址，不能复制、移动或由调用方修改成员。生命周期失败后，宿主接收错误并决定何时结束进程，在此之前残留 writer 可能访问的资源须保持有效，失败实例不可重用；以上示例因此使用静态实例。成功销毁只释放内部资源并复位对象，不释放实例存储，同一地址可再次初始化；未经过初始化的原始存储不可直接销毁。
- 同一实例支持多个生产者并发写入；初始化成功后模块表只读，不再增加或修改模块。模块 ID 和名称仅在该实例内要求唯一，重复项使初始化返回 `ERR_ALREADY_EXISTS`。
- `module_count = 0` 时允许 `modules_ptr = nullptr`；数量超过 `DOMINO_LOG_MAX_MODULES` 返回 `ERR_CAPACITY_EXCEEDED`，非零数量配 nullptr 返回 `ERR_NULL_POINTER`。模块名称必须非空且在 `DOMINO_LOG_MODULE_NAME_MAX` 字节内以 NUL 结尾，名称或等级不合法时返回 `ERR_INVALID_PARAM`；校验失败不会启动 writer 或分配内部资源。
- 模块 `min_level = 0` 继承实例默认等级，显式配置的模块等级独立生效，不受实例默认等级限制。
- 销毁前须停止并等待本实例所有生产者，且销毁不得与本实例其他 API 并发；销毁依次停止队列生产、唤醒等待线程、等待 writer 排空并刷新输出，最后销毁及释放内部队列，不影响其他实例。关闭排空期间的队列消费、写入错误，以及线程 join、刷新、关闭文件或队列销毁错误均上抛；首错立即返回，不继续释放后续资源。失败后再次销毁只返回已保存错误，不能用作清理重试。`nullptr` 日志调用及销毁均无操作；零初始化及已成功销毁对象可无操作销毁。
- stdout/stderr 由进程共享，实例之间没有全局输出顺序；文件输出需要隔离时请设置不同路径。tid 仍是进程级分配并在线程局部缓存的数字标识，不是系统线程 ID。
