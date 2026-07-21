add_executable(sbc_tests
    test_setup_sm.cpp
    test_dialog_sm.cpp
    test_options_sm.cpp
)

# Enable template instantiations for test types in state machine implementations
target_compile_definitions(sbc_tests PRIVATE __INCLUDE_MOCK_ACTIONS__)

target_precompile_headers(sbc_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sip/pch.hpp
)

target_include_directories(sbc_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sbc_tests PRIVATE
    third_party::catch2
    third_party::spdlog
    third_party::boost_sml
)

add_test(NAME sbc_tests COMMAND sbc_tests)
