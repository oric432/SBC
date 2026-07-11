add_executable(SbcEngine
    SbcEngine.cpp
    call/call_manager.cpp
    call/call_session.cpp
    config/settings.cpp
    router/message_router.cpp
    router/options_actions.cpp
    router/real_dialog_actions.cpp
    router/real_setup_actions.cpp
    routes/routes_client.cpp
    routes/routes_store.cpp
    rtp/RtpSession.cpp
    sip/pjsip_init.cpp
    sip/sdp_mangler.cpp
)

target_precompile_headers(SbcEngine PRIVATE
    pch.hpp
)

target_include_directories(SbcEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(SbcEngine PRIVATE
    project_warnings
    project_options
    third_party::spdlog
    third_party::boost_asio
    third_party::boost_system
    third_party::pjsip
    third_party::boost_sml
    third_party::glaze
)
