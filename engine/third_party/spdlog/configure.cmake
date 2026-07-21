set(BUILD_SHARED_LIBS OFF)
set(SPDLOG_HEADER_ONLY ON CACHE BOOL "Use header-only spdlog" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(spdlog)

add_library(tp_spdlog INTERFACE)
add_library(third_party::spdlog ALIAS tp_spdlog)
target_link_libraries(tp_spdlog INTERFACE spdlog::spdlog_header_only)
