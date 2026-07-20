add_library(core STATIC
    settings.cpp
)
target_include_directories(core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(core PUBLIC
    project_warnings
    project_options
    third_party::spdlog
    third_party::glaze
)
target_precompile_headers(core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp)
