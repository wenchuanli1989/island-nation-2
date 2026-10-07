file(GLOB_RECURSE DOMINO_ENGINE_REMOTE_LOCAL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
domino_exclude_playground_sources(DOMINO_ENGINE_REMOTE_LOCAL_SOURCES)

add_library(domino-engine-remote-local STATIC
    ${DOMINO_ENGINE_REMOTE_LOCAL_SOURCES}
)

target_include_directories(domino-engine-remote-local PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(domino-engine-remote-local PUBLIC
    domino-engine
)
