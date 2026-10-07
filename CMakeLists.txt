# Domino游戏服务器项目主CMake配置文件
cmake_minimum_required(VERSION 4.2.1)

# 项目名称和版本
project(Domino 
    VERSION 0.0.1
    LANGUAGES C
)

include(CTest)

# 生成compile_commands.json（供clangd使用）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/static-lib)

# ============================================================================
# 编译选项控制
# ============================================================================
# 是否启用针对本机 CPU 的极致优化（-march=native/-mtune=native）
# 警告：启用此选项后，编译出的二进制文件只能在构建它的机器或相同架构的 CPU 上运行
# 适用于：本地开发、性能测试、单机部署
# 不适用于：容器化部署、跨平台分发、CI/CD 构建通用二进制文件
option(DOMINO_ENABLE_NATIVE_ARCH
    "启用针对本机 CPU 的极致优化（-march=native/-mtune=native）。警告：会降低可移植性"
    OFF
)

# Playground 模式运行，用于测试模块内部实现逻辑（由 build.sh playground-build 传入 -D，见 playground/CMakeLists.txt）
option(DOMINO_PLAYGROUND_MODE "Playground 模式运行，用于测试模块内部实现逻辑（playground-build 时启用）" OFF)

# 云端/在线部署构建：定义项目级 DOMINO_CLOUD_BUILD（见 shared/include/domino_shared_build.h）
option(DOMINO_CLOUD_BUILD "云端/在线部署构建（定义 DOMINO_CLOUD_BUILD）" OFF)

# 当 DOMINO_PLAYGROUND_MODE=OFF 时，从源码列表中排除 src/playground.c
# 各模块在 GLOB 后调用：domino_exclude_playground_sources(SOURCES_VAR)
macro(domino_exclude_playground_sources var_name)
    if(NOT DOMINO_PLAYGROUND_MODE)
        list(REMOVE_ITEM ${var_name} "${CMAKE_CURRENT_SOURCE_DIR}/src/playground.c")
    else()
        message(STATUS "注意：Playground 模式运行，模块内部实现逻辑可能与实际运行逻辑不一致")
    endif()
endmacro()

# ============================================================================
# 创建 INTERFACE 库来承载项目统一的编译和链接选项
# 这样做的好处：
# 1. 只影响明确链接到此库的目标，不会污染第三方库或工具目标
# 2. 可以更灵活地控制哪些目标使用哪些选项
# 3. 符合现代 CMake 最佳实践
# ============================================================================
add_library(domino_options INTERFACE)

if(DOMINO_PLAYGROUND_MODE)
    target_compile_definitions(domino_options INTERFACE DOMINO_PLAYGROUND_MODE)
endif()

target_compile_definitions(domino_options INTERFACE
    $<$<BOOL:${DOMINO_CLOUD_BUILD}>:DOMINO_CLOUD_BUILD=1>
)

# 设置 C 标准（通过 INTERFACE 传播，替代全局设置）
target_compile_features(domino_options INTERFACE c_std_23)
set_property(TARGET domino_options PROPERTY INTERFACE_C_STANDARD 23)
set_property(TARGET domino_options PROPERTY INTERFACE_C_STANDARD_REQUIRED ON)
set_property(TARGET domino_options PROPERTY INTERFACE_C_EXTENSIONS OFF)

# 配置相关的编译选项（Debug/Release）
target_compile_options(domino_options INTERFACE
    $<$<CONFIG:Debug>:-glldb>       # 使用LLDB调试器
    $<$<CONFIG:Debug>:-O0>          # Debug模式禁用优化
    $<$<CONFIG:Debug>:-ftrapv>      # 在有符号整数溢出时触发陷阱
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer> # 禁用帧指针优化
    $<$<CONFIG:Release>:-O3>        # Release模式最高优化
    $<$<CONFIG:Release>:-DNDEBUG>   # 禁用 assert
    # 条件编译：只在明确启用 DOMINO_ENABLE_NATIVE_ARCH 时才使用 -march=native/-mtune=native
    # 默认关闭以保证可移植性（容器化部署、跨平台分发等场景）
    $<$<AND:$<CONFIG:Release>,$<BOOL:${DOMINO_ENABLE_NATIVE_ARCH}>>:-march=native> # 针对当前 CPU 启用全部可用指令集（SSE/AVX 之类），生成"只能在本机或同类 CPU 跑"的代码。
    $<$<AND:$<CONFIG:Release>,$<BOOL:${DOMINO_ENABLE_NATIVE_ARCH}>>:-mtune=native> # 调度/布局更适合本机 CPU
    $<$<CONFIG:Release>:-flto=thin> # 跨翻译单元整体优化，一般能再挤出不少性能（编译链接时间会明显变长）。
)

# 基础警告选项（所有配置都启用）
target_compile_options(domino_options INTERFACE
    -Wall                    # 启用所有常见警告
    -Wextra                  # 启用额外警告
    -Wpedantic               # 严格符合标准警告
    -Wformat=2               # 严格的格式字符串检查
    -Wformat-security        # 格式字符串安全检查
    -Wnull-dereference       # 空指针解引用警告
    -Wuninitialized          # 未初始化变量警告
    -Wconversion             # 类型转换警告
    -Wsign-conversion        # 符号转换警告
    -Wunused                 # 未使用变量/函数警告
)

# 强制要求全局变量和函数必须有声明
# -Wmissing-prototypes: 警告没有函数原型的函数定义（要求函数必须在头文件中声明）
# -Wmissing-declarations: 警告没有前向声明的函数定义（要求函数声明必须在定义之前可见）
# -Werror=missing-prototypes: 将缺少原型的警告视为错误，强制要求函数必须在头文件中声明
# -Werror=missing-declarations: 将缺少声明的警告视为错误
# -fno-common: 禁止在头文件中定义全局变量（tentative definitions），强制使用extern声明
target_compile_options(domino_options INTERFACE
    -Wmissing-prototypes
    -Wmissing-declarations
    -Werror=missing-prototypes
    -Werror=missing-declarations
    -fno-common
)


# Release模式链接选项
target_link_options(domino_options INTERFACE
    $<$<CONFIG:Release>:-Wl,-z,relro>           # 只读重定位
    $<$<CONFIG:Release>:-Wl,-z,now>             # 立即绑定
    $<$<CONFIG:Release>:-flto=thin>             # 链接时优化（与编译选项匹配）
)

# ============================================================================
# 注意：Sanitizer、ThreadSanitizer、gprof 等特殊编译选项
# 现在通过 CMakePresets.json 中的 CMAKE_C_FLAGS 和 CMAKE_EXE_LINKER_FLAGS 控制
# 这样做的好处：
# 1. 不依赖 CMAKE_BUILD_TYPE 的字符串匹配，支持多配置生成器
# 2. 所有编译选项集中在 Preset 中，一目了然
# 3. 可以灵活组合不同的 Build Type 和特殊选项
# ============================================================================

# 添加子目录（注意顺序：shared必须先构建，然后是core）
add_subdirectory(lib)
add_subdirectory(shared)
add_subdirectory(engine)
add_subdirectory(engine-remote/local)
add_subdirectory(engine-remote/server)
add_subdirectory(client-hub)
add_subdirectory(playground)



