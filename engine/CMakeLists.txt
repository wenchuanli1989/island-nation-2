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

target_link_libraries(domino-engine PUBLIC
    domino-shared
    domino_klib
    domino_yyjson
)
