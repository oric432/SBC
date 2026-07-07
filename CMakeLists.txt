cmake_minimum_required(VERSION 3.23)
project(sbc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(PreventInSourceBuilds)
include(StandardProjectSettings)

add_library(project_warnings INTERFACE)
add_library(project_options INTERFACE)

include(CompilerWarnings)
myproject_set_project_warnings(project_warnings FALSE "" "" "" "")

include(Sanitizers)
myproject_enable_sanitizers(project_options FALSE FALSE FALSE FALSE FALSE)

target_compile_options(project_options INTERFACE
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-system-headers;-Wno-#warnings>"
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-system-headers;-Wno-cpp>
)

include(${CMAKE_SOURCE_DIR}/third_party/spdlog.cmake)
include(${CMAKE_SOURCE_DIR}/third_party/catch2.cmake)
include(${CMAKE_SOURCE_DIR}/third_party/boost.cmake)
include(${CMAKE_SOURCE_DIR}/third_party/pjsip.cmake)

add_executable(sbc
    src/main.cpp
)

target_precompile_headers(sbc PRIVATE
    src/pch.hpp
)

target_include_directories(sbc PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(sbc SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/boost-sml/include
)

target_link_libraries(sbc PRIVATE
    project_warnings
    project_options
    spdlog::spdlog_header_only
    Boost::asio
    Boost::system
    pjsip_all
)

include(ClangFormat)
add_clang_format_target(format ${CMAKE_SOURCE_DIR}/src)

add_subdirectory(tests)
