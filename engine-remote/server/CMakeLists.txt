add_executable(domino-engine-remote-server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger/entry.c
)

target_link_libraries(domino-engine-remote-server
    domino-engine
)
