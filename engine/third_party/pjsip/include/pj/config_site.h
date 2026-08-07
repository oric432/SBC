#ifndef CONFIG_SITE_H
#define CONFIG_SITE_H

// Equivalent to --enable-epoll
#undef PJ_HAS_EPOLL
#define PJ_HAS_EPOLL 1

// Equivalent to --disable-ssl
#undef PJ_HAS_SSL_SOCK
#define PJ_HAS_SSL_SOCK 0

// Equivalent to --disable-sound
#undef PJMEDIA_HAS_AUDIODEV
#define PJMEDIA_HAS_AUDIODEV 0

// Equivalent to --disable-video, --disable-libyuv, --disable-v4l2, --disable-openh264, --disable-sdl
#undef PJMEDIA_HAS_VIDEO
#define PJMEDIA_HAS_VIDEO 0

// Equivalent to --disable-opencore-amr
#undef PJMEDIA_HAS_OPENCORE_AMR_NB_CODEC
#define PJMEDIA_HAS_OPENCORE_AMR_NB_CODEC 0
#undef PJMEDIA_HAS_OPENCORE_AMR_WB_CODEC
#define PJMEDIA_HAS_OPENCORE_AMR_WB_CODEC 0

// Equivalent to --disable-silk
#undef PJMEDIA_HAS_SILK_CODEC
#define PJMEDIA_HAS_SILK_CODEC 0

// Equivalent to --disable-opus
#undef PJMEDIA_HAS_OPUS_CODEC
#define PJMEDIA_HAS_OPUS_CODEC 0

// Equivalent to --disable-libwebrtc
#undef PJMEDIA_HAS_WEBRTC_AEC
#define PJMEDIA_HAS_WEBRTC_AEC 0

#endif // CONFIG_SITE_H
