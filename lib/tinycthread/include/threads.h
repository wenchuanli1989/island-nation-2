#ifndef DOMINO_TINYCTHREAD_THREADS_H
#define DOMINO_TINYCTHREAD_THREADS_H

/**
 * @brief 以 TinyCThread 提供项目使用的标准线程接口：macOS SDK 缺少 <threads.h>；Linux 的 tsan 预设借它让
 * ThreadSanitizer 拦截到底层 pthread 调用。
 * @note glibc 的 C23 <stdlib.h> 自带 once_flag／call_once，而 TinyCThread 把两者定义为 pthread 的宏；
 * 先包含 <stdlib.h>，让 glibc 的声明在宏生效前完成。
 */
#if !defined(__APPLE__)
#include <stdlib.h>
#endif
#include "../tinycthread.h"

#endif
