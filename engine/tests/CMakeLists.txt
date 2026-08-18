add_executable(sbc_mock_tests
    test_mock_sbc_actions.cpp
)

target_include_directories(sbc_mock_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/sip
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sbc_mock_tests PRIVATE
    third_party::catch2
)

add_test(NAME sbc_mock_tests COMMAND sbc_mock_tests)
