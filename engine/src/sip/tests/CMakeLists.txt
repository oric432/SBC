add_executable(sip_tests
    test_routes_manager.cpp
    test_extract_utils.cpp
)

target_include_directories(sip_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sip_tests PRIVATE
    sip
    third_party::catch2
    third_party::spdlog
    third_party::boost_asio
)

add_test(NAME sip_tests COMMAND sip_tests)

