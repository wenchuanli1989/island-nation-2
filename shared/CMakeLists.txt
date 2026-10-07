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

target_link_libraries(domino-shared PUBLIC
    domino_options
)
