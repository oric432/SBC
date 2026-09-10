add_executable(net_tests
    test_media_bridge.cpp
    test_codec_session.cpp
    test_audio_transcoder.cpp
    test_dtmf_pt_relay.cpp
)

target_include_directories(net_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(net_tests PRIVATE
    net
    third_party::catch2
    third_party::spdlog
    third_party::boost_asio
    third_party::pjsip
)

add_test(NAME net_tests COMMAND net_tests)

