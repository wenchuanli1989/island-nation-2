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

if(BUILD_TESTING)
    add_executable(domino-engine-log-safety-test
        tests/log_safety_test.c
    )
    target_link_libraries(domino-engine-log-safety-test PRIVATE
        domino-engine
    )
    add_test(NAME domino-engine-log-safety COMMAND domino-engine-log-safety-test)

    add_executable(domino-engine-log-instances-test
        tests/log_instances_test.c
    )
    target_link_libraries(domino-engine-log-instances-test PRIVATE
        domino-engine
    )
    add_test(NAME domino-engine-log-instances COMMAND domino-engine-log-instances-test)
    set_tests_properties(domino-engine-log-instances PROPERTIES TIMEOUT 30)

    add_executable(domino-engine-storage-lifecycle-test
        tests/storage_lifecycle_test.c
    )
    target_include_directories(domino-engine-storage-lifecycle-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(domino-engine-storage-lifecycle-test PRIVATE
        domino-engine
    )
    add_test(NAME domino-engine-storage-lifecycle COMMAND domino-engine-storage-lifecycle-test)

    add_executable(domino-engine-thread-queue-test
        tests/thread_queue_test.c
    )
    target_include_directories(domino-engine-thread-queue-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(domino-engine-thread-queue-test PRIVATE
        domino-engine
    )
    add_test(NAME domino-engine-thread-queue COMMAND domino-engine-thread-queue-test)
    set_tests_properties(domino-engine-thread-queue PROPERTIES TIMEOUT 30)

    add_executable(domino-engine-timer-id-test
        tests/timer_id_test.c
    )
    target_include_directories(domino-engine-timer-id-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(domino-engine-timer-id-test PRIVATE
        domino-engine
    )
    add_test(NAME domino-engine-timer-id COMMAND domino-engine-timer-id-test)
    set_tests_properties(domino-engine-timer-id PROPERTIES TIMEOUT 30)

    add_executable(domino-engine-timer-wait-test
        tests/timer_wait_test.c
    )
    target_include_directories(domino-engine-timer-wait-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(domino-engine-timer-wait-test PRIVATE
        domino-engine
    )
    add_test(NAME domino-engine-timer-wait COMMAND domino-engine-timer-wait-test)
    set_tests_properties(domino-engine-timer-wait PROPERTIES TIMEOUT 30)

    add_executable(domino-engine-timer-cancel-test
        tests/timer_cancel_test.c
    )
    target_include_directories(domino-engine-timer-cancel-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(domino-engine-timer-cancel-test PRIVATE
        domino-engine
    )
    add_test(NAME domino-engine-timer-cancel COMMAND domino-engine-timer-cancel-test)
    set_tests_properties(domino-engine-timer-cancel PROPERTIES TIMEOUT 30)

    add_executable(domino-engine-queue-exit-test
        tests/queue_exit_test.c
    )
    target_include_directories(domino-engine-queue-exit-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(domino-engine-queue-exit-test PRIVATE
        domino-engine
    )
    add_test(NAME domino-engine-queue-exit COMMAND domino-engine-queue-exit-test)
    set_tests_properties(domino-engine-queue-exit PROPERTIES TIMEOUT 15)
endif()
