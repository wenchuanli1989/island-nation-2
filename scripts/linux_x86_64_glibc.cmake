# 为本机 Linux x86_64 构建选择独立安装的 glibc sysroot。
include_guard(GLOBAL)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
    message(FATAL_ERROR "独立 glibc 工具链仅支持本机 Linux x86_64 构建")
endif()

set(DOMINO_GLIBC_ROOT "$ENV{DOMINO_GLIBC_ROOT}" CACHE PATH "独立 glibc sysroot 的根目录")
file(REAL_PATH "${DOMINO_GLIBC_ROOT}" DOMINO_GLIBC_ROOT)
set(domino_glibc_lib_dir "${DOMINO_GLIBC_ROOT}/usr/lib/x86_64-linux-gnu")
set(domino_glibc_loader "${domino_glibc_lib_dir}/ld-linux-x86-64.so.2")

foreach(domino_glibc_file IN ITEMS
    "${DOMINO_GLIBC_ROOT}/usr/include/time.h"
    "${domino_glibc_lib_dir}/libc.so"
    "${domino_glibc_lib_dir}/libc.so.6"
    "${domino_glibc_lib_dir}/libgcc_s.so.1"
    "${domino_glibc_loader}"
)
    if(NOT EXISTS "${domino_glibc_file}")
        message(FATAL_ERROR "独立 glibc 安装不完整，缺少 ${domino_glibc_file}")
    endif()
endforeach()

set(CMAKE_SYSROOT "${DOMINO_GLIBC_ROOT}")
# 继续使用宿主机 GCC 的编译器运行库；C 头文件、启动对象与 libc 来自 sysroot。
set(CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN "/usr")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES DOMINO_GLIBC_ROOT)

# 同时设置 ELF 加载器和运行期库搜索路径，程序可直接启动。
set(domino_glibc_loader_flag "-Wl,--dynamic-linker=\"${domino_glibc_loader}\"")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${domino_glibc_loader_flag}")
# Sanitizer 等预设会显式设置 linker flags，仍须保留所选加载器。
if(DEFINED CMAKE_EXE_LINKER_FLAGS)
    string(FIND "${CMAKE_EXE_LINKER_FLAGS}" "${domino_glibc_loader_flag}" domino_glibc_loader_flag_index)
    if(domino_glibc_loader_flag_index EQUAL -1)
        string(APPEND CMAKE_EXE_LINKER_FLAGS " ${domino_glibc_loader_flag}")
    endif()
endif()
list(APPEND CMAKE_BUILD_RPATH "${domino_glibc_lib_dir}")
