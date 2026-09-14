add_library(net STATIC
    pj_status_error.cpp
    rtp/rtp_cpp.cpp
    rtp/media_bridge.cpp
    rtp/rtp_inactivity_timer.cpp
    rtp/pjmedia_endpoint.cpp
    rtp/codec_session.cpp
    rtp/audio_transcoder.cpp
    rtp/transcoded_rtp_stream.cpp
    rtp/transcode_session.cpp
)
target_compile_definitions(net PRIVATE RTPCPP_USE_BOOST_ASIO)

target_include_directories(net PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(net PUBLIC
    project_options
    third_party::boost_asio
    third_party::spdlog
    third_party::pjsip
    protocols
)
target_precompile_headers(net PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp)

if(BUILD_TESTS)
    add_subdirectory(rtp/tests)
endif()
