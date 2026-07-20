add_library(tp_catch2 STATIC
    ${CMAKE_CURRENT_LIST_DIR}/catch_amalgamated.cpp
    ${CMAKE_CURRENT_LIST_DIR}/catch_main.cpp
)

target_include_directories(tp_catch2 SYSTEM PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

add_library(third_party::catch2 ALIAS tp_catch2)
