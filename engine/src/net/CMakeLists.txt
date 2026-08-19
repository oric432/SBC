add_library(net STATIC
    rtp/RtpCpp.cpp
    rtp/MediaBridge.cpp
    rtp/RtpInactivityTimer.cpp
)
target_compile_definitions(net PRIVATE RTPCPP_USE_BOOST_ASIO)

target_include_directories(net PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(net PUBLIC
    project_options
    third_party::boost_asio
    third_party::spdlog
)
target_precompile_headers(net PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
