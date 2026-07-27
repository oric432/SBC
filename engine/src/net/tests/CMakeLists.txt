add_executable(net_tests
    test_media_bridge.cpp
)

target_include_directories(net_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(net_tests PRIVATE
    net
    third_party::catch2
    third_party::spdlog
    third_party::boost_asio
)

add_test(NAME net_tests COMMAND net_tests)
