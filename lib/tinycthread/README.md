# TinyCThread

仅用于补齐 macOS SDK 缺失的 `<threads.h>`，项目代码继续使用标准的
`thrd_*`、`mtx_*`、`cnd_*` 等线程接口。Linux 使用系统标准库。

- 上游：https://github.com/tinycthread/tinycthread
- 固定提交：`6957fc8383d6c7db25b60b8c849b29caab1caaee`
- `tinycthread.c`、`tinycthread.h` 原样来自该提交的 `source/`，保留文件开头的许可证。
- `include/threads.h` 是 Domino 提供的头文件转接入口，仅加入 macOS 目标的包含路径。
- macOS 目标定义 `_DARWIN_C_SOURCE`，避免上游的 `_XOPEN_SOURCE` 限制 SDK 的接口声明。
- TinyCThread 的线程返回码数值不必与系统实现相同，调用方应与 `thrd_success`、
  `thrd_timedout` 等符号比较，不能假定成功值为 `0`。

更新时保留上游许可证，并核对项目使用的线程、互斥锁和条件变量接口。
