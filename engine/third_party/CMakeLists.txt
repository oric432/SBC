include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# 1. Include all dependency declarations (defines where to download them)
if(BUILD_ENGINE OR BUILD_TESTS)
    include(${CMAKE_CURRENT_LIST_DIR}/spdlog/declare.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/boost/declare.cmake)
endif()

if(BUILD_ENGINE)
    include(${CMAKE_CURRENT_LIST_DIR}/pjsip/declare.cmake)
endif()

if(BUILD_ENGINE OR BUILD_TOOLS)
    include(${CMAKE_CURRENT_LIST_DIR}/glaze/declare.cmake)
endif()

# 2. Include all dependency configurations (sets options and retrieves/builds them)
if(BUILD_ENGINE OR BUILD_TESTS)
    include(${CMAKE_CURRENT_LIST_DIR}/spdlog/configure.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/boost/configure.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/boost-sml/configure.cmake)
endif()

if(BUILD_TESTS)
    include(${CMAKE_CURRENT_LIST_DIR}/catch2/configure.cmake)
endif()

if(BUILD_ENGINE)
    include(${CMAKE_CURRENT_LIST_DIR}/pjsip/configure.cmake)
endif()

if(BUILD_ENGINE OR BUILD_TOOLS)
    include(${CMAKE_CURRENT_LIST_DIR}/glaze/configure.cmake)
endif()
