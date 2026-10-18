file(GLOB_RECURSE DOMINO_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
domino_exclude_playground_sources(DOMINO_ENGINE_SOURCES)

add_library(domino-engine STATIC
    ${DOMINO_ENGINE_SOURCES}
)

target_include_directories(domino-engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(domino-engine PRIVATE
    -mrdrnd  # 启用RDRAND指令集支持
)

find_package(Threads REQUIRED)

target_link_libraries(domino-engine PUBLIC
    domino-shared
    domino_klib
    domino_yyjson
    Threads::Threads
)

# macOS SDK 缺少 <threads.h>，由第三方 TinyCThread 提供标准线程接口。
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_library(domino_tinycthread STATIC
        ${PROJECT_SOURCE_DIR}/lib/tinycthread/tinycthread.c
    )
    target_compile_features(domino_tinycthread PRIVATE c_std_11)
    target_compile_definitions(domino_tinycthread PUBLIC _DARWIN_C_SOURCE)
    target_include_directories(domino_tinycthread SYSTEM PUBLIC
        ${PROJECT_SOURCE_DIR}/lib/tinycthread/include
    )
    target_link_libraries(domino_tinycthread PUBLIC Threads::Threads)
    target_link_libraries(domino-engine PUBLIC domino_tinycthread)
endif()
