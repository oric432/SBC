FetchContent_MakeAvailable(glaze)

add_library(tp_glaze INTERFACE)
add_library(third_party::glaze ALIAS tp_glaze)
target_link_libraries(tp_glaze INTERFACE glaze::glaze)
