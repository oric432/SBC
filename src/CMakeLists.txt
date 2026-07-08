add_executable(sbc
    main.cpp
    call/call_manager.cpp
    call/call_session.cpp
    router/message_router.cpp
    router/options_actions.cpp
    router/real_dialog_actions.cpp
    router/real_setup_actions.cpp
    rtp/RtpSession.cpp
    sip/pjsip_init.cpp
    sip/sdp_mangler.cpp
)

target_precompile_headers(sbc PRIVATE
    pch.hpp
)

target_include_directories(sbc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sbc PRIVATE
    project_warnings
    project_options
    third_party::spdlog
    third_party::boost_asio
    third_party::boost_system
    third_party::pjsip
    third_party::boost_sml
)
