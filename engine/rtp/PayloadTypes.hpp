#pragma once
#include <cstdint>
#include <string_view>

/* Payload types (PT) from for audio encoding from RFC 3551.
PT   encoding    media type  clock rate   channels
                    name                    (Hz)
               ___________________________________________________
               0    PCMU        A            8,000       1
               1    reserved    A
               2    reserved    A
               3    GSM         A            8,000       1
               4    G723        A            8,000       1
               5    DVI4        A            8,000       1
               6    DVI4        A           16,000       1
               7    LPC         A            8,000       1
               8    PCMA        A            8,000       1
               9    G722        A            8,000       1
               10   L16         A           44,100       2
               11   L16         A           44,100       1
               12   QCELP       A            8,000       1
               13   CN          A            8,000       1
               14   MPA         A           90,000       (see text)
               15   G728        A            8,000       1
               16   DVI4        A           11,025       1
               17   DVI4        A           22,050       1
               18   G729        A            8,000       1
               19   reserved    A
               20   unassigned  A
               21   unassigned  A
               22   unassigned  A
               23   unassigned  A
               dyn  G726-40     A            8,000       1
               dyn  G726-32     A            8,000       1
               dyn  G726-24     A            8,000       1
               dyn  G726-16     A            8,000       1
               dyn  G729D       A            8,000       1
               dyn  G729E       A            8,000       1
               dyn  GSM-EFR     A            8,000       1
               dyn  L8          A            var.        var.
               dyn  RED         A                        (see text)
               dyn  VDVI        A            var.        1
*/

/* Payload types (PT) from for video and combined encoding from RFC 3551.
PT      encoding    media type  clock rate
                       name                    (Hz)
               _____________________________________________
               24      unassigned  V
               25      CelB        V           90,000
               26      JPEG        V           90,000
               27      unassigned  V
               28      nv          V           90,000
               29      unassigned  V
               30      unassigned  V
               31      H261        V           90,000
               32      MPV         V           90,000
               33      MP2T        AV          90,000
               34      H263        V           90,000
               35-71   unassigned  ?
               72-76   reserved    N/A         N/A
               77-95   unassigned  ?
               96-127  dynamic     ?
               dyn     H263-1998   V           90,000
*/

namespace RtpCpp {


constexpr int MinDynamicRtp = 96;
constexpr int MaxDynamicRtp = 127;

enum class AudioPt : std::uint8_t {
    // Audio payload types
    kPCMU = 0,
    kGSM = 3,
    kG723 = 4,
    kDVI4_8000 = 5,
    kDVI4_16000 = 6,
    kLPC = 7,
    kPCMA = 8,
    kG722 = 9,
    kL16_2CH = 10,
    kL16_1CH = 11,
    kQCELP = 12,
    kCN = 13,
    kMPA = 14,
    kG728 = 15,
    kDVI4_11025 = 16,
    kDVI4_22050 = 17,
    kG729 = 18,

};

enum class VideoPt : std::uint8_t {
    // Video payload types
    kCelB = 25,
    kJPEG = 26,
    kNV = 28,
    kH261 = 31,
    kMP2T = 33, // Audio Video
    kMPV = 32,
    kH263 = 34,
};

// Dynamic payload types (values 96–127), no assigned numeric value

enum class AudioDynamicPayloadType : std::uint8_t {
    // Defined dynamic audio payload types.
    kG726_40,
    kG726_32,
    kG726_24,
    kG726_16,
    kG729D,
    kG729E,
    kGSM_EFR,
    kL8,
    kRED,
    kVDVI,

};

enum class VideoDynamicPayloadType : std::uint8_t {
    // Defined dynamic video payload types.
    kH263
};

enum class InactivePT : uint8_t {
    // Audio pt
    PT_1 = 1, // reserved
    PT_2 = 2, // reserved
    PT_19 = 19, // reserved
    PT_20 = 20, // unassigned
    PT_21 = 21, // unassigned
    PT_22 = 22, // unassigned
    PT_23 = 23, // unassigned

    // Video pt
    PT_24 = 24, // unassigned
    PT_27 = 27, // unassigned
    PT_29 = 29, // unassigned
    PT_30 = 30, // unassigned
    // 35–71 unassigned
    PT_35 = 35,
    PT_36 = 36,
    PT_37 = 37,
    PT_38 = 38,
    PT_39 = 39,
    PT_40 = 40,
    PT_41 = 41,
    PT_42 = 42,
    PT_43 = 43,
    PT_44 = 44,
    PT_45 = 45,
    PT_46 = 46,
    PT_47 = 47,
    PT_48 = 48,
    PT_49 = 49,
    PT_50 = 50,
    PT_51 = 51,
    PT_52 = 52,
    PT_53 = 53,
    PT_54 = 54,
    PT_55 = 55,
    PT_56 = 56,
    PT_57 = 57,
    PT_58 = 58,
    PT_59 = 59,
    PT_60 = 60,
    PT_61 = 61,
    PT_62 = 62,
    PT_63 = 63,
    PT_64 = 64,
    PT_65 = 65,
    PT_66 = 66,
    PT_67 = 67,
    PT_68 = 68,
    PT_69 = 69,
    PT_70 = 70,
    PT_71 = 71,
    // 72–76 reserved
    PT_72 = 72,
    PT_73 = 73,
    PT_74 = 74,
    PT_75 = 75,
    PT_76 = 76,
    // 77–95 unassigned
    PT_77 = 77,
    PT_78 = 78,
    PT_79 = 79,
    PT_80 = 80,
    PT_81 = 81,
    PT_82 = 82,
    PT_83 = 83,
    PT_84 = 84,
    PT_85 = 85,
    PT_86 = 86,
    PT_87 = 87,
    PT_88 = 88,
    PT_89 = 89,
    PT_90 = 90,
    PT_91 = 91,
    PT_92 = 92,
    PT_93 = 93,
    PT_94 = 94,
    PT_95 = 95
};

inline bool is_valid_pt(const std::uint8_t pt) noexcept {
    switch (InactivePT{pt}) {
    case InactivePT::PT_1:
    case InactivePT::PT_2:
    case InactivePT::PT_19:
    case InactivePT::PT_20:
    case InactivePT::PT_21:
    case InactivePT::PT_22:
    case InactivePT::PT_23:
    case InactivePT::PT_24:
    case InactivePT::PT_27:
    case InactivePT::PT_29:
    case InactivePT::PT_30:
    case InactivePT::PT_35:
    case InactivePT::PT_36:
    case InactivePT::PT_37:
    case InactivePT::PT_38:
    case InactivePT::PT_39:
    case InactivePT::PT_40:
    case InactivePT::PT_41:
    case InactivePT::PT_42:
    case InactivePT::PT_43:
    case InactivePT::PT_44:
    case InactivePT::PT_45:
    case InactivePT::PT_46:
    case InactivePT::PT_47:
    case InactivePT::PT_48:
    case InactivePT::PT_49:
    case InactivePT::PT_50:
    case InactivePT::PT_51:
    case InactivePT::PT_52:
    case InactivePT::PT_53:
    case InactivePT::PT_54:
    case InactivePT::PT_55:
    case InactivePT::PT_56:
    case InactivePT::PT_57:
    case InactivePT::PT_58:
    case InactivePT::PT_59:
    case InactivePT::PT_60:
    case InactivePT::PT_61:
    case InactivePT::PT_62:
    case InactivePT::PT_63:
    case InactivePT::PT_64:
    case InactivePT::PT_65:
    case InactivePT::PT_66:
    case InactivePT::PT_67:
    case InactivePT::PT_68:
    case InactivePT::PT_69:
    case InactivePT::PT_70:
    case InactivePT::PT_71:
    case InactivePT::PT_72:
    case InactivePT::PT_73:
    case InactivePT::PT_74:
    case InactivePT::PT_75:
    case InactivePT::PT_76:
    case InactivePT::PT_77:
    case InactivePT::PT_78:
    case InactivePT::PT_79:
    case InactivePT::PT_80:
    case InactivePT::PT_81:
    case InactivePT::PT_82:
    case InactivePT::PT_83:
    case InactivePT::PT_84:
    case InactivePT::PT_85:
    case InactivePT::PT_86:
    case InactivePT::PT_87:
    case InactivePT::PT_88:
    case InactivePT::PT_89:
    case InactivePT::PT_90:
    case InactivePT::PT_91:
    case InactivePT::PT_92:
    case InactivePT::PT_93:
    case InactivePT::PT_94:
    case InactivePT::PT_95: return false;
    }

    return true;
}

inline bool is_audio_pt(const std::uint8_t pt) noexcept {
    using enum AudioPt;
    switch (AudioPt{pt}) {
    case kPCMU:
    case kGSM:
    case kG723:
    case kDVI4_8000:
    case kDVI4_16000:
    case kLPC:
    case kPCMA:
    case kG722:
    case kL16_2CH:
    case kL16_1CH:
    case kQCELP:
    case kCN:
    case kMPA:
    case kG728:
    case kDVI4_11025:
    case kDVI4_22050:
    case kG729: return true;

    default: return false;
    }
}

inline bool is_video_pt(const std::uint8_t pt) noexcept {
    using enum VideoPt;

    switch (VideoPt{pt}) {
    case kCelB:
    case kJPEG:
    case kNV:
    case kH261:
    case kMPV:
    case kH263: return true;
    case kMP2T: break;
    }

    return false;
}

inline std::string_view audio_pt_tostring(const std::uint8_t pt) noexcept {
    using enum AudioPt;
    switch (AudioPt{pt}) {
    case kPCMU: return "PCMU";
    case kGSM: return "GSM";
    case kG723: return "G723";
    case kDVI4_8000: return "DVI (8000 hz)";
    case kDVI4_16000: return "DVI (16000 hz)";
    case kLPC: return "LPC";
    case kPCMA: return "PCMA";
    case kG722: return "G722";
    case kL16_2CH: return "L16 (dual channel)";
    case kL16_1CH: return "L16 (single channel)";
    case kQCELP: return "QCELP";
    case kCN: return "CN";
    case kMPA: return "MPA";
    case kG728: return "G729";
    case kDVI4_11025: return "DVI4 (11020 hz)";
    case kDVI4_22050: return "DVI (22050 hz)";
    case kG729: return "G729";
    }

    return {};
}

inline std::string_view video_pt_tostring(const std::uint8_t pt) noexcept {
    using enum VideoPt;
    switch (VideoPt{pt}) {
    case kCelB: return "CelB";
    case kJPEG: return "JPEG";
    case kNV: return "NV";
    case kH261: return "H261";
    case kMPV: return "MPV";
    case kH263: return "H263";
    case kMP2T: return "MP2T"; break;
    }

    return {};
}

inline bool is_dynamic_rtp(const std::uint8_t pt) noexcept {
    return pt >= MinDynamicRtp && pt <= MaxDynamicRtp;
}

} // namespace RtpCpp
