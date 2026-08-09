set(BUILD_SHARED_LIBS OFF)
set(SPDLOG_HEADER_ONLY ON CACHE BOOL "Use header-only spdlog" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

sbc_build_dependency(spdlog)

sbc_expose_third_party(third_party::spdlog spdlog::spdlog_header_only)
