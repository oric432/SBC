include(ExternalProject)
include(ProcessorCount)

ProcessorCount(PJSIP_JOBS)

# Use the number of available CPU cores for parallel building
if(PJSIP_JOBS EQUAL 0)
    set(PJSIP_JOBS 4)
endif()

# Determine the suffix PJSIP appends to library names.
# PJSIP uses autoconf's config.guess, which produces the canonical GNU triple
# (e.g. x86_64-unknown-linux-gnu) — independent of which C compiler is used.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PJSIP_TARGET "${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PJSIP_TARGET "${CMAKE_SYSTEM_PROCESSOR}-apple-darwin")
else()
    message(FATAL_ERROR "Unsupported platform for PJSIP: ${CMAKE_SYSTEM_NAME}")
endif()
message(STATUS "PJSIP target suffix: ${PJSIP_TARGET}")

set(PJSIP_INSTALL_DIR "${CMAKE_BINARY_DIR}/_pjsip_install")

# Ordered for static linking: most-dependent first
set(_pj_lib_names
    pjsip-simple
    pjsip-ua
    pjsip
    pjnath
    pjmedia-codec
    pjmedia
    pjlib-util
    pj
)

# List of PJSIP static libraries to link against
set(_pj_byproducts "")
foreach(_lib ${_pj_lib_names})
    list(APPEND _pj_byproducts "${PJSIP_INSTALL_DIR}/lib/lib${_lib}-${PJSIP_TARGET}.a")
endforeach()

# Download and build PJSIP using ExternalProject_Add
ExternalProject_Add(pjsip_external
    URL "https://github.com/pjsip/pjproject/archive/refs/tags/2.14.1.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP ON

    BUILD_IN_SOURCE 1

    CONFIGURE_COMMAND
    ${CMAKE_COMMAND} -E env CC=clang CXX=clang++
    <SOURCE_DIR>/configure
    --prefix=<INSTALL_DIR>
    --disable-sound
    --disable-video
    --disable-resample
    --disable-opencore-amr
    --disable-silk
    --disable-opus
    --disable-libyuv
    --disable-v4l2
    --disable-openh264
    --disable-libwebrtc
    --disable-sdl
    --disable-ssl
    --enable-epoll

    BUILD_COMMAND sh -c "make dep && make -j${PJSIP_JOBS}"

    INSTALL_COMMAND make install

    INSTALL_DIR "${PJSIP_INSTALL_DIR}"

    BUILD_BYPRODUCTS "${_pj_byproducts}"
)

# Aggregate INTERFACE target — link this to pull in all of PJSIP
add_library(pjsip_all INTERFACE)
add_dependencies(pjsip_all pjsip_external)
target_include_directories(pjsip_all SYSTEM INTERFACE "${PJSIP_INSTALL_DIR}/include")

foreach(_lib ${_pj_lib_names})
    add_library(pjsip::${_lib} STATIC IMPORTED GLOBAL)
    set_target_properties(pjsip::${_lib} PROPERTIES
        IMPORTED_LOCATION "${PJSIP_INSTALL_DIR}/lib/lib${_lib}-${PJSIP_TARGET}.a"
    )
    add_dependencies(pjsip::${_lib} pjsip_external)
    target_link_libraries(pjsip_all INTERFACE pjsip::${_lib})
endforeach()

# System libraries PJSIP depends on (nsl excluded — not available on glibc 2.32+)
target_link_libraries(pjsip_all INTERFACE uuid m rt pthread dl)
