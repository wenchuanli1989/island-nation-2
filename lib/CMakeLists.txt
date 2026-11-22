add_library(domino_klib INTERFACE)
target_include_directories(domino_klib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(domino_yyjson STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/yyjson/yyjson.c
)
target_include_directories(domino_yyjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
# yyjson.c 内部使用 `#include "yyjson.h"`，需要额外把目录本身加进来
target_include_directories(domino_yyjson PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/yyjson
)

target_link_libraries(domino_yyjson PRIVATE domino_options)

# macOS SDK 缺少 <threads.h>，由 TinyCThread 提供标准线程接口。
# tsan 预设在 Linux 上也启用：glibc 的 thrd_*／mtx_*／cnd_* 在 libc 内部直接调用 pthread 实现，
# 绕过 ThreadSanitizer 的 pthread 拦截，线程状态未建立即崩溃；改经 TinyCThread 调用 pthread 公共接口后可被拦截。
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR DOMINO_USE_TINYCTHREAD)
    add_library(domino_tinycthread STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tinycthread/tinycthread.c
    )
    target_compile_features(domino_tinycthread PRIVATE c_std_11)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_compile_definitions(domino_tinycthread PUBLIC _DARWIN_C_SOURCE)
    else()
        # 与 include/threads.h 相同：glibc <stdlib.h> 的 once_flag／call_once 须先于 TinyCThread 的同名宏声明。
        target_compile_options(domino_tinycthread PRIVATE -include stdlib.h)
    endif()
    target_include_directories(domino_tinycthread SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tinycthread/include
    )
    target_link_libraries(domino_tinycthread PUBLIC Threads::Threads)
endif()
