# Third-Party Dependencies Integration Guide

This directory manages all external and third-party libraries for the project. 

---

## Folder Structure & Architecture

Every dependency is organized into its own subdirectory under `third_party/`:
```text
third_party/
├── <dependency_name>/
│   ├── declare.cmake       # Defines retrieval (FetchContent_Declare/URLs)
│   └── configure.cmake     # Sets options, calls MakeAvailable, and exposes targets
└── CMakeLists.txt          # Orchestrator (includes all declares first, then configures)
```

---

## How to Use a Dependency

To use a third-party dependency, link against it in your target's `CMakeLists.txt` using the `third_party::` namespace prefix:

```cmake
# Example CMakeLists.txt
target_link_libraries(my_target PRIVATE
    third_party::<dependency_name>
)
```

> [!NOTE]
> Modern CMake automatically propagates include paths, compile flags, and preprocessor definitions. Linking via `target_link_libraries` is **all you need**—you do not need to call `target_include_directories` manually.

By convention:
* The target name is the dependency's directory name prefixed with `third_party::` (e.g., `third_party::spdlog` or `third_party::pjsip`).
* For dependencies that expose multiple components (like Boost), check their target definitions inside their `configure.cmake` file (e.g., `third_party::boost_asio` and `third_party::boost_system`).

---

## How to Add a New Dependency

To integrate a new dependency, follow these steps:

### Step 1: Create the directory
```bash
mkdir third_party/<new_dependency>
```

### Step 2: Define Source (`declare.cmake`)
Create `third_party/<new_dependency>/declare.cmake` and call `FetchContent_Declare`:
```cmake
FetchContent_Declare(
    <new_dependency>
    GIT_REPOSITORY https://github.com/username/<new_dependency>.git
    GIT_TAG v1.0.0
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
```

### Step 3: Configure Target (`configure.cmake`)
Create `third_party/<new_dependency>/configure.cmake`. Instead of raw CMake commands, use our helper macros provided in `Utils.cmake` to build the dependency without its tests, and expose it cleanly as a `SYSTEM` target (which suppresses third-party compiler warnings):

```cmake
# 1. Build the dependency (automatically disables tests/examples during fetch)
sbc_build_dependency(<new_dependency>)

# 2. Expose the official target under the third_party:: namespace as a SYSTEM library
sbc_expose_third_party(third_party::<new_dependency> <official_library_target_name>)
```

### Step 4: Register in Orchestrator
Include both files in the root `third_party/CMakeLists.txt`:
```cmake
# 1. Include declarations
include(${CMAKE_CURRENT_LIST_DIR}/<new_dependency>/declare.cmake)

# 2. Include configurations
include(${CMAKE_CURRENT_LIST_DIR}/<new_dependency>/configure.cmake)
```
Now, `third_party::<new_dependency>` is ready to be linked anywhere in the project!
