# TinyCThread

用于补齐 macOS SDK 缺失的 `<threads.h>`，项目代码继续使用标准的
`thrd_*`、`mtx_*`、`cnd_*` 等线程接口。Linux 平时使用系统标准库；`tsan` 预设设置
`DOMINO_USE_TINYCTHREAD=ON` 改用 TinyCThread，因为 glibc 的 C11 线程函数在 libc 内部直接调用 pthread
实现，ThreadSanitizer 拦截不到。

- 上游：https://github.com/tinycthread/tinycthread
- 固定提交：`6957fc8383d6c7db25b60b8c849b29caab1caaee`
- `tinycthread.c`、`tinycthread.h` 原样来自该提交的 `source/`，保留文件开头的许可证。
- `include/threads.h` 是 Domino 提供的头文件转接入口，仅加入启用 TinyCThread 的目标的包含路径。
  glibc 的 C23 `<stdlib.h>` 自带 `once_flag`／`call_once`，与上游的同名宏冲突，因此非 macOS 平台先包含
  `<stdlib.h>`，编译 `tinycthread.c` 时也以 `-include stdlib.h` 预先包含。
- macOS 目标定义 `_DARWIN_C_SOURCE`，避免上游的 `_XOPEN_SOURCE` 限制 SDK 的接口声明。
- TinyCThread 的线程返回码数值不必与系统实现相同，调用方应与 `thrd_success`、
  `thrd_timedout` 等符号比较，不能假定成功值为 `0`。

更新时保留上游许可证，并核对项目使用的线程、互斥锁和条件变量接口。
