add_executable(sbc_tests
    test_setup_sm.cpp
    test_dialog_sm.cpp
)

target_include_directories(sbc_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_include_directories(sbc_tests SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/boost-sml/include
)

target_link_libraries(sbc_tests PRIVATE
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(sbc_tests)
