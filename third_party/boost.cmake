include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

set(BOOST_INCLUDE_LIBRARIES asio system CACHE STRING "" FORCE)
set(BOOST_ENABLE_CMAKE ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  fetchcontent_declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SYSTEM
  )
else()
  fetchcontent_declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
  )
endif()

FetchContent_MakeAvailable(Boost)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  foreach(_boost_target Boost::headers Boost::asio Boost::system)
    if(TARGET ${_boost_target})
      get_target_property(_aliased_target ${_boost_target} ALIASED_TARGET)
      if(_aliased_target)
        set_property(TARGET ${_aliased_target} PROPERTY SYSTEM TRUE)
      else()
        set_property(TARGET ${_boost_target} PROPERTY SYSTEM TRUE)
      endif()
    endif()
  endforeach()
endif()
