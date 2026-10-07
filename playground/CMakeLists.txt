# 允许通过外部参数指定源码目录，默认为 playground/src
set(PLAYGROUND_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE PATH "Playground源码目录")



file(GLOB_RECURSE PLAYGROUND_SOURCES CONFIGURE_DEPENDS
    "${PLAYGROUND_SOURCE_DIR}/*.c"
)

message(STATUS "Playground 源码目录: ${PLAYGROUND_SOURCE_DIR}")

add_executable(domino-playground
    ${PLAYGROUND_SOURCES}
)

target_link_libraries(domino-playground
    domino-client-hub
)
