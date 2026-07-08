---
name: add_third_party_dependency
description: Triggers when the user asks to add or install a new third-party dependency (C++ library, package, etc.) to the project.
---

# Instructions for Adding a Third-Party Dependency

When the user asks you to add a new third-party dependency to this project, follow this exact architecture to maintain consistency.

## 1. Create the Dependency Directory
Create a dedicated folder for the dependency inside `third_party/`:
`third_party/<dep_name>/`

## 2. Declare the Dependency (`declare.cmake`)
Create `third_party/<dep_name>/declare.cmake`. This file should **only** contain the `FetchContent_Declare` command for the dependency. Do not call `FetchContent_MakeAvailable` here.

Example (`third_party/fmt/declare.cmake`):
```cmake
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
)
```

## 3. Configure and Expose the Target (`configure.cmake`)
Create `third_party/<dep_name>/configure.cmake`. This file should:
1. Call `FetchContent_MakeAvailable(<dep_name>)`
2. Create an `ALIAS` or `INTERFACE` target that exposes the library under the unified `third_party::` namespace.

Example (`third_party/fmt/configure.cmake`):
```cmake
# Fetch and build
FetchContent_MakeAvailable(fmt)

# Expose under unified namespace
if(TARGET fmt::fmt)
    add_library(third_party::fmt ALIAS fmt::fmt)
endif()
```

## 4. Register the Scripts in `third_party/CMakeLists.txt`
You must add the new `declare.cmake` and `configure.cmake` scripts to `third_party/CMakeLists.txt` in two distinct phases:

1. **Phase 1 (Declarations)**: Add `include(${CMAKE_CURRENT_LIST_DIR}/<dep_name>/declare.cmake)` to the declarations block.
2. **Phase 2 (Configurations)**: Add `include(${CMAKE_CURRENT_LIST_DIR}/<dep_name>/configure.cmake)` to the configurations block.

*Note: Declarations must happen before any configurations to ensure dependencies of dependencies resolve correctly.*

## 5. Update Documentation
Add a brief entry for the new dependency in `third_party/README.md`. Mention the underlying source/version and the exposed target name (e.g., `third_party::fmt`).

## 6. Apply to Targets
Once configured, you can link the dependency to any target in `src/CMakeLists.txt` or `tests/CMakeLists.txt` by using the namespaced wrapper:
```cmake
target_link_libraries(sbc PRIVATE third_party::fmt)
```
