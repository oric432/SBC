FetchContent_MakeAvailable(Catch2)

# Propagate Catch2's CMake module path (for include(Catch)) to the parent scope
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${catch2_SOURCE_DIR}/extras" PARENT_SCOPE)

add_library(tp_catch2 INTERFACE)
add_library(third_party::catch2 ALIAS tp_catch2)
target_link_libraries(tp_catch2 INTERFACE Catch2::Catch2WithMain)
