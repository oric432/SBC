include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

set(BUILD_SHARED_LIBS OFF)
set(SPDLOG_HEADER_ONLY ON CACHE BOOL "Use header-only spdlog" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    spdlog
    URL "https://github.com/gabime/spdlog/archive/refs/tags/v1.16.0.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP NO
)

FetchContent_MakeAvailable(spdlog)
