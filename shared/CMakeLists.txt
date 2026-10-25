file(GLOB_RECURSE DOMINO_SHARED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
domino_exclude_playground_sources(DOMINO_SHARED_SOURCES)

add_library(domino-shared STATIC
    ${DOMINO_SHARED_SOURCES}
)

target_include_directories(domino-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)

target_link_libraries(domino-shared PUBLIC
    domino_options
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
    target_link_libraries(domino-shared PUBLIC domino_tinycthread)
endif()
