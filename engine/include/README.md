# engine 公共头文件说明

`engine/include` 提供引擎生命周期、消息通信和多实例日志的公共 API。日志实现位于 `engine/src/logger/log.c`，engine、client-hub 和 engine-remote 可各自持有独立实例。

## 文件职责

| 文件 | 说明 |
|------|------|
| `domino_engine.h` | 引擎初始化、运行、退出、时间冻结/解冻及消息收发接口。 |
| `domino_engine_log.h` | 多实例异步日志服务 API；每个实例有独立队列和 writer 线程。 |

## 多实例日志

`DominoLogger` 的完整结构体定义由公共头文件提供，实例存储由调用方分配。engine、client-hub、engine-remote/local 和 engine-remote/server 分别持有自己的实例；也可使用局部、静态或调用方分配的存储初始化更多实例。每个实例独立配置队列容量、默认级别和可选日志文件，独立管理 writer、模块注册表和 seq。

```c
#include <stdbool.h>

#include "domino_engine_log.h"

int logWithTwoServices(void) {
    DominoLogger first_logger;
    DominoLogger second_logger;
    bool first_initialized_flag = false;
    bool second_initialized_flag = false;
    const DominoLogModule modules[] = {{.module_id = 0U, .name = "core"}};
    const size_t module_count = sizeof(modules) / sizeof(modules[0]);
    int result = dominoLogInit((DominoLogConfig){.file_path = "first.log"}, modules, module_count, &first_logger);
    if (result != 0) {
        goto cleanup;
    }
    first_initialized_flag = true;
    result = dominoLogInit((DominoLogConfig){.file_path = "second.log"}, modules, module_count, &second_logger);
    if (result != 0) {
        goto cleanup;
    }
    second_initialized_flag = true;

    const DominoLogSourceLocation loc = {.file = __FILE__, .line = __LINE__, .func = __func__};
    dominoLogMsg(&first_logger, 0U, DOMINO_LOG_LEVEL_INFO, loc, "first service");
    dominoLogDestroy(&first_logger);
    first_initialized_flag = false;
    dominoLogMsg(&second_logger, 0U, DOMINO_LOG_LEVEL_INFO, loc, "second service continues");

cleanup:
    if (second_initialized_flag) {
        dominoLogDestroy(&second_logger);
    }
    if (first_initialized_flag) {
        dominoLogDestroy(&first_logger);
    }
    return result;
}
```

- `dominoLogInit(config, modules_ptr, module_count, &logger)` 在初始化阶段一次性校验并复制完整模块列表，再分配内部资源并启动 writer；返回后不借用传入模块数组。存储无需预先清零，初始化失败会释放内部资源并复位对象，之后可重试。已成功初始化且未销毁的实例不得再次初始化。
- 每个实例通过 `queue_ptr` 持有独立的 `DominoThreadQueue`，队列存储与同步对象由 queue 模块管理。`buffer_size = 0` 使用默认容量 10240；容量必须不超过 `UINT32_MAX`，且分配大小不得溢出。队列容量固定，满载时日志生产者阻塞等待空位。
- 调用方提供的存储须一直有效，直到 `dominoLogDestroy(&logger)` 返回；实例必须保持固定地址，不能复制、移动或由调用方修改成员。局部实例须在离开作用域前销毁。销毁只释放内部资源并复位对象，不释放实例存储，同一地址可再次初始化；未经过初始化的原始存储不可直接销毁。
- 同一实例支持多个生产者并发写入；初始化成功后模块表只读，不再增加或修改模块。模块 ID 和名称仅在该实例内要求唯一，重复项使初始化返回 `ERR_ALREADY_EXISTS`。
- `module_count = 0` 时允许 `modules_ptr = nullptr`；数量超过 `DOMINO_LOG_MAX_MODULES` 返回 `ERR_OUT_OF_RANGE`，非零数量配 nullptr 返回 `ERR_NULL_POINTER`。模块名称必须非空且在 `DOMINO_LOG_MODULE_NAME_MAX` 字节内以 NUL 结尾，名称或等级不合法时返回 `ERR_INVALID_PARAM`；校验失败不会启动 writer 或分配内部资源。
- 模块 `min_level = 0` 继承实例默认等级，显式配置的模块等级独立生效，不受实例默认等级限制。
- 销毁前须停止并等待本实例所有生产者，且销毁不得与本实例其他 API 并发；销毁依次停止队列生产、唤醒等待线程、等待 writer 排空并刷新输出，最后销毁及释放内部队列，不影响其他实例。`nullptr` 日志调用及销毁均无操作；初始化失败后的对象及已销毁对象可再次销毁。
- stdout/stderr 由进程共享，实例之间没有全局输出顺序；文件输出需要隔离时请设置不同路径。tid 仍是进程级分配并在线程局部缓存的数字标识，不是系统线程 ID。
