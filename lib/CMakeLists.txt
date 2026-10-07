add_library(domino_klib INTERFACE)
target_include_directories(domino_klib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(domino_yyjson STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/yyjson/yyjson.c
)
target_include_directories(domino_yyjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
# yyjson.c 内部使用 `#include "yyjson.h"`，需要额外把目录本身加进来
target_include_directories(domino_yyjson PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/yyjson
)

target_link_libraries(domino_yyjson PRIVATE domino_options)
