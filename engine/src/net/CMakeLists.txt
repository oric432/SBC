add_library(net STATIC
    rtp/RtpSession.cpp
)
target_include_directories(net PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(net PUBLIC
    project_warnings
    project_options
    third_party::boost_asio
    third_party::spdlog
)
target_precompile_headers(net PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp)
