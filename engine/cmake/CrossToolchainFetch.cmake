# Cross-compilation toolchain (only fetched when a preset/-D sets
# CMAKE_TOOLCHAIN_FILE, e.g. the cross-linux-gnu/cross-linux-musl presets).
# Must be include()'d before project() so the toolchain file exists by the
# time CMake processes CMAKE_TOOLCHAIN_FILE.
if(CMAKE_TOOLCHAIN_FILE)
    include(FetchContent)
    FetchContent_Declare(
        PortableCcToolchain
        URL "https://github.com/CACI-International/cpp-toolchain/releases/download/v2026.06.26/cmake_portable_cc_toolchain-v2026.06.26.tar.gz"
        SOURCE_DIR ${CMAKE_BINARY_DIR}/portable_cc_toolchain
        URL_HASH SHA256=4f6f7b9c54cc0439e189d85dc042a33411df9bc1266739e09f10c43d7fdb2f8b
    )
    FetchContent_MakeAvailable(PortableCcToolchain)

    # The toolchain file re-downloads its LLVM/sysroot assets every time CMake
    # processes CMAKE_TOOLCHAIN_FILE, including inside each isolated
    # try_compile() (compiler detection, and every PJSIP/Boost
    # check_include_file()-style check). Each of those defaults
    # FETCHCONTENT_BASE_DIR to its own throwaway scratch dir, so without this
    # every single check re-fetches the whole toolchain from GitHub. Pin it to
    # a real shared path and forward it into try_compile so later checks hit
    # the cache this first fetch just populated.
    set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "" FORCE)
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES FETCHCONTENT_BASE_DIR)
endif()
