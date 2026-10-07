file(GLOB_RECURSE DOMINO_CLIENT_HUB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
domino_exclude_playground_sources(DOMINO_CLIENT_HUB_SOURCES)

add_library(domino-client-hub STATIC
    ${DOMINO_CLIENT_HUB_SOURCES}
)

target_include_directories(domino-client-hub PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(domino-client-hub PUBLIC
    domino-engine-remote-local
)

if(BUILD_TESTING)
    # 将 server 日志适配器放入同一进程，验证四个模块的实例所有权互不影响。
    add_executable(domino-module-log-lifecycle-test
        tests/log_lifecycle_test.c
        ../engine-remote/server/src/logger/entry.c
    )
    target_link_libraries(domino-module-log-lifecycle-test PRIVATE
        domino-client-hub
    )
    add_test(NAME domino-module-log-lifecycle COMMAND domino-module-log-lifecycle-test)
    set_tests_properties(domino-module-log-lifecycle PROPERTIES TIMEOUT 30)
endif()
