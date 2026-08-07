# Ensure our custom config_site.h is found first
include_directories(BEFORE ${CMAKE_CURRENT_LIST_DIR}/include)

# Workaround upstream CMake syntax error in FindUPNP.cmake (missing quotes)
set(PJNATH_WITH_UPNP OFF CACHE BOOL "Disable UPnP to avoid CMake error" FORCE)

# Workaround C++26 compiler errors in bundled WebRTC by disabling it at the CMake level
set(PJMEDIA_WITH_WEBRTC_AEC OFF CACHE BOOL "Disable to avoid C++26 errors" FORCE)
set(PJMEDIA_WITH_WEBRTC_AEC3 OFF CACHE BOOL "Disable to avoid C++26 errors" FORCE)

# Set resample backend to none since we don't need audio
set(PJMEDIA_WITH_RESAMPLE "none" CACHE STRING "Disable resample backend" FORCE)

# Disable video, ffmpeg, and specific media features not needed for an SBC (which only proxies RTP/SDP)
set(PJMEDIA_HAS_VIDEO OFF CACHE BOOL "Disable video" FORCE)
set(PJMEDIA_WITH_VIDEO OFF CACHE BOOL "Disable video" FORCE)
set(PJMEDIA_WITH_FFMPEG OFF CACHE BOOL "Disable ffmpeg" FORCE)
set(PJMEDIA_WITH_LIBYUV OFF CACHE BOOL "Disable libyuv" FORCE)

# Disable Audio Devices (SBC doesn't play or record audio locally)
set(PJMEDIA_WITH_AUDIODEV OFF CACHE BOOL "Disable audio devices" FORCE)

# Disable ALL third-party Audio Codecs and features to save compilation time (SBC proxies RTP directly)
set(PJMEDIA_WITH_SRTP OFF CACHE BOOL "Disable SRTP" FORCE)
set(PJMEDIA_WITH_SPEEX_AEC OFF CACHE BOOL "Disable Speex AEC" FORCE)
set(PJMEDIA_WITH_WEBRTC_AEC OFF CACHE BOOL "Disable WebRTC AEC" FORCE)
set(PJMEDIA_WITH_WEBRTC_AEC3 OFF CACHE BOOL "Disable WebRTC AEC3" FORCE)
set(PJMEDIA_WITH_G711_CODEC OFF CACHE BOOL "Disable G711" FORCE)
set(PJMEDIA_WITH_L16_CODEC OFF CACHE BOOL "Disable L16" FORCE)
set(PJMEDIA_WITH_GSM_CODEC OFF CACHE BOOL "Disable GSM" FORCE)
set(PJMEDIA_WITH_SPEEX_CODEC OFF CACHE BOOL "Disable SPEEX" FORCE)
set(PJMEDIA_WITH_ILBC_CODEC OFF CACHE BOOL "Disable iLBC" FORCE)
set(PJMEDIA_WITH_G722_CODEC OFF CACHE BOOL "Disable G722" FORCE)
set(PJMEDIA_WITH_G7221_CODEC OFF CACHE BOOL "Disable G722.1" FORCE)
set(PJMEDIA_WITH_OPENCORE_AMRNB_CODEC OFF CACHE BOOL "Disable AMRNB" FORCE)
set(PJMEDIA_WITH_OPENCORE_AMRWB_CODEC OFF CACHE BOOL "Disable AMRWB" FORCE)
set(PJMEDIA_WITH_SILK_CODEC OFF CACHE BOOL "Disable SILK" FORCE)
set(PJMEDIA_WITH_OPUS_CODEC OFF CACHE BOOL "Disable OPUS" FORCE)
set(PJMEDIA_WITH_BCG729_CODEC OFF CACHE BOOL "Disable BCG729" FORCE)
set(PJMEDIA_WITH_LYRA_CODEC OFF CACHE BOOL "Disable Lyra" FORCE)

# Fetch and build PJSIP
sbc_build_dependency(pjsip)

# Expose under the unified third_party:: namespace
# We create an aggregate INTERFACE target to ensure all module includes are propagated,
# since PJProject's CMake sets some internal dependencies as PRIVATE, breaking downstream includes.
add_library(third_party_pjsip_agg INTERFACE)
add_library(third_party::pjsip ALIAS third_party_pjsip_agg)

set(PJ_TARGETS pjsip pjsip-simple pjsip-ua pjmedia pjmedia-codec pjnath pjlib-util pjlib)
foreach(tgt ${PJ_TARGETS})
    if(TARGET ${tgt})
        target_link_libraries(third_party_pjsip_agg INTERFACE ${tgt})
        
        # Elevate includes to SYSTEM to silence compiler/clang-tidy warnings
        get_target_property(tgt_includes ${tgt} INTERFACE_INCLUDE_DIRECTORIES)
        if(tgt_includes)
            set_property(TARGET third_party_pjsip_agg APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${tgt_includes}")
        endif()
    endif()
endforeach()
