if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  FetchContent_Declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SYSTEM
  )
else()
  FetchContent_Declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
  )
endif()
