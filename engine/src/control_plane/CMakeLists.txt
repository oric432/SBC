add_library(control_plane STATIC
    control_plane_client.cpp
)
target_include_directories(control_plane PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(control_plane PUBLIC
    project_options
    core
    sip
    protocols
    third_party::boost_asio
    third_party::boost_system
    third_party::boost_beast
    third_party::glaze
)
target_precompile_headers(control_plane PRIVATE ${CMAKE_SOURCE_DIR}/src/core/pch.hpp)

if(BUILD_TESTS)
    add_executable(control_plane_tests
        tests/test_control_plane_client.cpp
    )

    target_include_directories(control_plane_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(control_plane_tests PRIVATE
        control_plane
        third_party::catch2
        third_party::spdlog
    )

    add_test(NAME control_plane_tests COMMAND control_plane_tests)
endif()
