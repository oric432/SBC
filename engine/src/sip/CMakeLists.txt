add_library(sip STATIC
    stack/pjsip_init.cpp
    stack/sdp.cpp
    stack/inv_session.cpp
    call/call_manager.cpp
    call/call_session.cpp
    call/offer_answer_exchange.cpp
    call/setup_actions.cpp
    call/dialog_actions.cpp
    call/handlers/bye_handler.cpp
    call/handlers/reinvite_handler.cpp
    call/offer_answer_actions.cpp
    router/message_router.cpp
    router/sip_request_actions.cpp
    router/extract_utils.cpp
    route_table/routes_store.cpp
    route_table/routes_manager.cpp
    sm/sm_runner.cpp
    sm/options_actions.cpp
)
target_include_directories(sip PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sip PUBLIC
    project_options
    core
    net
    protocols
    third_party::pjsip
    third_party::boost_sml
    third_party::boost_asio
    third_party::boost_system
    third_party::glaze
)
target_precompile_headers(sip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp)

if(BUILD_TESTS)
    add_executable(sip_tests
        call/handlers/tests/test_reinvite_handler.cpp
        route_table/tests/test_routes_manager.cpp
        router/tests/test_extract_utils.cpp
        sm/tests/test_setup_sm.cpp
        sm/tests/test_dialog_sm.cpp
        sm/tests/test_offer_answer_sm.cpp
        sm/tests/test_options_sm.cpp
        stack/tests/test_sdp.cpp
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
endif()
