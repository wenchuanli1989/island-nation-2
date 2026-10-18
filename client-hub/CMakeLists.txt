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
