add_executable(sip_tests
    test_routes_manager.cpp
    test_extract_utils.cpp
    test_setup_sm.cpp
    test_dialog_sm.cpp
    test_options_sm.cpp
)

# Enable template instantiations for test types in state machine implementations
target_compile_definitions(sip_tests PRIVATE __INCLUDE_MOCK_ACTIONS__)

target_precompile_headers(sip_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sip/pch.hpp
)

target_include_directories(sip_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/sip
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(sip_tests PRIVATE
    sip
    third_party::catch2
    third_party::spdlog
    third_party::boost_asio
)

add_test(NAME sip_tests COMMAND sip_tests)
