add_library(sip STATIC
    stack/pjsip_init.cpp
    stack/sdp_mangler.cpp
    call/call_manager.cpp
    call/call_session.cpp
    router/message_router.cpp
    router/options_actions.cpp
    router/real_dialog_actions.cpp
    router/real_setup_actions.cpp
    routes/routes_client.cpp
    routes/routes_store.cpp
)
target_include_directories(sip PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sip PUBLIC
    project_warnings
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
