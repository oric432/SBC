add_executable(sbc_tests
    test_setup_sm.cpp
    test_dialog_sm.cpp
    test_options_sm.cpp
)

# Enable template instantiations for test types in state machine implementations
target_compile_definitions(sbc_tests PRIVATE __INCLUDE_MOCK_ACTIONS__)

target_precompile_headers(sbc_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pch.hpp
)

target_include_directories(sbc_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_include_directories(sbc_tests SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/boost-sml/include
)

target_link_libraries(sbc_tests PRIVATE
    Catch2::Catch2WithMain
    spdlog::spdlog_header_only
)

include(Catch)
catch_discover_tests(sbc_tests)
