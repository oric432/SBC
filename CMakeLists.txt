# ==============================================================================
# 1. Project Initialization
# ==============================================================================
cmake_minimum_required(VERSION 3.23)
project(sbc LANGUAGES C CXX)

# ==============================================================================
# 2. C++ Standard Settings
# ==============================================================================
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# 3. Testing Setup
# ==============================================================================
enable_testing()
include(CTest)

# ==============================================================================
# 4. CMake Modules & Core Utilities
# ==============================================================================
# Add our custom cmake/ directory to the module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(PreventInSourceBuilds)
include(StandardProjectSettings)

# ==============================================================================
# 5. Global Configuration Options
# ==============================================================================
# Expose simple toggles to the user for sanitizers and warnings
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_LSAN "Enable Leak Sanitizer" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ==============================================================================
# 6. Project Interface Libraries (Settings Aggregators)
# ==============================================================================
# These INTERFACE libraries hold all warning and sanitizer flags.
# Any target that links against them will automatically inherit these settings.
add_library(project_warnings INTERFACE)
add_library(project_options INTERFACE)

# Configure warnings
include(CompilerWarnings)
myproject_set_project_warnings(project_warnings ${WARNINGS_AS_ERRORS} "" "" "" "")

# Configure sanitizers
include(Sanitizers)
myproject_enable_sanitizers(project_options ${ENABLE_ASAN} ${ENABLE_LSAN} ${ENABLE_UBSAN} ${ENABLE_TSAN} FALSE)

# Suppress annoying third-party system headers warnings
target_compile_options(project_options INTERFACE
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-system-headers;-Wno-#warnings>"
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-system-headers;-Wno-cpp>
)

# ==============================================================================
# 7. Code Formatting
# ==============================================================================
include(ClangFormat)
add_clang_format_target(format ${CMAKE_SOURCE_DIR}/src)

# ==============================================================================
# 8. Subdirectories (Dependencies & Project Source)
# ==============================================================================
# Load third-party dependencies first so their targets are available
add_subdirectory(third_party)

# Load the main application source
add_subdirectory(src)

# Load helper tools (like the schema generator)
add_subdirectory(tools)

# Load the test suite
add_subdirectory(tests)
