cmake_minimum_required(VERSION 3.23)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/CrossToolchainFetch.cmake")

project(sbc LANGUAGES C CXX)

if(CMAKE_TOOLCHAIN_FILE)
    # CMake's Clang-ASM module still emits the legacy single-dash
    # -gcc-toolchain spelling for CMAKE_ASM_COMPILER_EXTERNAL_TOOLCHAIN; this
    # clang build (22.1.8) rejects it outright ("unknown argument"), even
    # though the C/CXX modules already emit the modern --gcc-toolchain= form
    # and compile fine. --sysroot alone (set separately via CMAKE_SYSROOT) is
    # all hand-written .S sources (e.g. Boost.Context) need.
    set(CMAKE_ASM_COMPILER_EXTERNAL_TOOLCHAIN "")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Options
option(BUILD_ENGINE "Build the main SbcEngine executable" ON)
option(BUILD_TESTS "Build the test suite" ON)
option(BUILD_TOOLS "Build helper tools" ON)
option(BUILD_SCHEMA_GENERATOR "Build the schema generator tool" ON)

# Quality Options
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_LSAN "Enable Leak Sanitizer" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ENABLE_CLANG_TIDY "Enable clang tidy during build" OFF)

if(BUILD_TESTS)
    enable_testing()
    include(CTest)
endif()

# CMake Modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(PreventInSourceBuilds)
include(StandardProjectSettings)
include(CompilerWarnings)
include(ClangTidy)
include(Sanitizers)
include(ClangFormat)

add_clang_format_target(format ${CMAKE_SOURCE_DIR}/src)
add_clang_format_check_target(format-check ${CMAKE_SOURCE_DIR}/src)

# Project Options Aggregator
add_library(project_options INTERFACE)
myproject_set_project_warnings(project_options ${WARNINGS_AS_ERRORS} "" "" "" "")
myproject_enable_sanitizers(project_options ${ENABLE_ASAN} ${ENABLE_LSAN} ${ENABLE_UBSAN} ${ENABLE_TSAN} FALSE)

target_compile_options(project_options INTERFACE
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-system-headers;-Wno-#warnings>"
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-system-headers;-Wno-cpp>
)

# Dependencies
add_subdirectory(third_party)

# Enable clang-tidy globally after third_party so external deps are not linted
enable_clang_tidy_globally(${ENABLE_CLANG_TIDY})

# Internal Source
if(BUILD_ENGINE OR BUILD_TESTS)
    add_subdirectory(src/core)
    add_subdirectory(src/net)
    add_subdirectory(src/sip)
    add_subdirectory(src/protocols)
endif()

if(BUILD_ENGINE)
    add_executable(SbcEngine
        src/SbcEngine.cpp
        src/sbc_app.cpp
    )

    target_precompile_headers(SbcEngine PRIVATE
        src/core/pch.hpp
    )

    target_include_directories(SbcEngine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(SbcEngine PRIVATE
        project_options
        core
        net
        sip
        protocols
        third_party::spdlog
        third_party::boost_asio
        third_party::boost_system
        third_party::pjsip
        third_party::boost_sml
        third_party::glaze
    )
endif()

if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
