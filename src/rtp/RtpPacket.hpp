#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include "endianness.hpp"


namespace RtpCpp {


template <typename C>
concept ContiguousBuffer =
    std::ranges::contiguous_range<C> && std::same_as<std::ranges::range_value_t<C>, std::uint8_t>;

template <typename C>
concept ResizableContiguousBuffer = ContiguousBuffer<C> && requires(C& c, std::size_t n) { c.resize(n); };


struct FixedHeader {
    // Fixed fields
    std::uint32_t timestamp_ = 0;
    std::uint32_t ssrc_ = 0;
    std::uint16_t sequence_number_ = 0;
    std::uint8_t csrc_count_ = 0;
    std::uint8_t payload_type_ = 0;
    bool is_extended_ = false;
    bool is_marked_ = false;
    bool is_padded_ = false;

    // size is based on csrc_count_
    std::uint32_t* csrc_ = nullptr;
};

enum ExtensionId : std::uint16_t;
enum ExtensionLength : std::uint16_t;

struct ExtensionHeader {
    std::uint16_t id_{};
    std::uint16_t length_{};


    void reset() {
        id_ = 0;
        length_ = 0;
    }

    [[nodiscard]] std::size_t data_size_bytes() const { return static_cast<std::size_t>(length_) * 4; }
    [[nodiscard]] std::size_t size_bytes() const { return 4 + data_size_bytes(); }
};

enum class Result : std::uint8_t {
    kSuccess,
    kBufferTooSmall,
    kFixedBufferTooSmall,
    kParseBufferOverflow,
    kParseExtensionOverflow,
    kInvalidHeaderLength,
    kInvalidRtpHeader,
    kInvalidCsrcCount,
};


template <ContiguousBuffer B>
class RtpPacket {
private:
    // RTP header has minimum size of 12 bytes.
    static constexpr std::size_t kFixedRTPSize = 12;
    static constexpr std::size_t kMaxCsrcIds = 15;
    static constexpr std::uint8_t kRtpVersion = 2;
    static constexpr std::size_t kMaxFixedPktSize = kFixedRTPSize + (kMaxCsrcIds * 4);

    // using ElementT = typename B::value_type;
    using CsrcList = std::array<std::uint32_t, kMaxCsrcIds>;
    using PayloadSpan = std::span<std::uint8_t>;
    using ExtensionSpan = std::span<std::uint8_t>;
    using PacketBuffer = std::span<std::uint8_t>;

    struct Version {
        static constexpr std::size_t kOffset = 0;
        static constexpr std::uint8_t kMask = 0b1100'0000U;
        static constexpr std::uint8_t kShift = 6U;
    };

    struct PaddingBit {
        static constexpr std::size_t kOffset = 0;
        static constexpr std::uint8_t kMask = 0b0010'0000U;
        static constexpr std::uint8_t kShift = 5U;
    };

    struct ExtensionBit {
        static constexpr std::size_t kOffset = 0U;
        static constexpr std::uint8_t kMask = 0b0001'0000U;
        static constexpr std::uint8_t kShift = 4U;
    };

    struct CsrcCount {
        static constexpr std::size_t kOffset = 0U;
        static constexpr std::uint8_t kMask = 0b0000'1111U;
    };

    struct MarkerBit {
        static constexpr std::size_t kOffset = 1U;
        static constexpr std::uint8_t kMask = 0b1000'0000U;
        static constexpr std::uint8_t kShift = 7U;
    };

    struct PayloadType {
        static constexpr std::size_t kOffset = 1U;
        static constexpr std::uint8_t kMask = 0b0111'1111U;
    };


    struct SequenceNumber {
        static constexpr std::size_t kOffset = 2U;
    };

    struct Timestamp {
        static constexpr std::size_t kOffset = 4U;
    };

    struct Ssrc {
        static constexpr std::size_t kOffset = 8U;
    };


    struct StagingCsrcList {
        explicit StagingCsrcList(RtpPacket<B>& rtp_packet)
            : rtp_packet_(rtp_packet){};

        StagingCsrcList add_csrc(std::uint32_t ssrc) {
            if (!is_already_staged(ssrc) && rtp_packet_.staged_csrc_list_.size() < kMaxCsrcIds) {
                rtp_packet_.staged_csrc_list_.emplace_back(ssrc);
            }

            return *this;
        }

        StagingCsrcList remove_csrc(std::uint32_t ssrc) {
            if (is_already_staged(ssrc) && rtp_packet_.staged_csrc_list_.size() > 0) {
                auto itr = std::ranges::find(rtp_packet_.staged_csrc_list_, ssrc);
                if (itr != rtp_packet_.staged_csrc_list_.end()) {
                    rtp_packet_.staged_csrc_list_.erase(itr);
                }
            }

            return *this;
        }

        Result flush() {
            Result res = rtp_packet_.set_csrc(rtp_packet_.staged_csrc_list_.size());
            if (res == Result::kSuccess) {
                rtp_packet_.write_csrc();
                rtp_packet_.staged_csrc_list_.clear();
            }
            return res;
        }


    private:
        bool is_already_staged(std::uint32_t ssrc) {
            return std::ranges::contains(rtp_packet_.staged_csrc_list_, ssrc);
        }
        RtpPacket<B>& rtp_packet_;
    };

public:
    RtpPacket()
        requires ResizableContiguousBuffer<B>
        : buffer_(kFixedRTPSize)
        , packet_size_(kFixedRTPSize) {}

    explicit RtpPacket(const B& buffer)
        : buffer_(buffer){};

    RtpPacket()
        requires(std::is_same_v<B, std::span<std::uint8_t>>)
    = delete;

    RtpPacket() = default;


    [[nodiscard]] Result parse(const B& buffer) {
        buffer_ = buffer;
        on_parse(buffer_.size());
        return parse_pkt();
    }

    [[nodiscard]] Result parse(B&& buffer) {
        buffer_ = std::move(buffer);
        on_parse(buffer_.size());
        return parse_pkt();
    }

    [[nodiscard]] Result parse(B&& buffer, std::size_t packet_size) {
        buffer_ = std::move(buffer);
        on_parse(packet_size);
        return parse_pkt();
    }


    [[nodiscard]] Result parse(const B& buffer, std::size_t packet_size) {
        buffer_ = buffer;
        on_parse(packet_size);
        return parse_pkt();
    }

    [[nodiscard]] Result parse(std::size_t packet_size) {
        on_parse(packet_size);
        return parse_pkt();
    }

    [[nodiscard]] Result parse() {
        reset();
        return parse_pkt();
    }

private:
    [[nodiscard]] Result parse_pkt() {
        // std::size_t buffer_size = buffer_bytes_size();
        if (packet_size_ < kFixedRTPSize) {
            return Result::kBufferTooSmall;
        }

        payload_offset_ = kFixedRTPSize;

        // Version is the first 2 bits in octet 0
        const std::uint8_t version = (buffer_[Version::kOffset] & Version::kMask) >> Version::kShift;


        // RFC 3550 RTP version is 2.
        if (version != kRtpVersion) {
            return Result::kInvalidRtpHeader;
        }

        // Padding bit is the 2 bit in octet 0
        const bool is_padded =
            static_cast<int>(((buffer_[PaddingBit::kOffset] & PaddingBit::kMask) >> PaddingBit::kShift) != 0U) > 0;
        if (is_padded) {
            padding_bytes_ = buffer_[packet_size_ - 1];

            // Padding amount need to be at least 1 additional octet.
            if (padding_bytes_ == 0) {
                return Result::kInvalidRtpHeader;
            }

            // Check if padding amount exceed packet size
            if (packet_size_ < padding_bytes_ + kFixedRTPSize) {
                return Result::kParseBufferOverflow;
            }
        }

        // extension bit is the 3 bit in octet 0
        fields_.is_extended_ = (((buffer_[ExtensionBit::kOffset] & ExtensionBit::kMask) >> ExtensionBit::kShift) != 0U);


        // csrc count is 4 bits at offset 4 octet 0
        fields_.csrc_count_ = buffer_[CsrcCount::kOffset] & CsrcCount::kMask;

        payload_offset_ += static_cast<std::size_t>(csrc_list_size());
        if (payload_offset_ > packet_size_) {
            return Result::kParseBufferOverflow;
        }

        extract_csrc();

        // marker is the first bit at octet 0.
        fields_.is_marked_ = ((buffer_[MarkerBit::kOffset] >> MarkerBit::kShift) != 0U);


        // payload type is 7 bits at offset 2 octet 1
        fields_.payload_type_ = buffer_[PayloadType::kOffset] & PayloadType::kMask;

#ifdef RFC_3551
        if (!is_valid_pt(fields_.payload_type_)) {
            return false;
        }
#endif

        //  sequrence number is 16 bits at offset 16 octet 2 and 3
        fields_.sequence_number_ =
            read_big_endian<decltype(fields_.sequence_number_)>(&buffer_[SequenceNumber::kOffset]);

        // timestamp is 32 bits at offset 32 octet: 4, 5, 6, 7,
        fields_.timestamp_ = read_big_endian<decltype(fields_.timestamp_)>(&buffer_[Timestamp::kOffset]);

        // ssrc identifier is 32 bits at offset 64 octet: 8, 9 ,10 ,11
        fields_.ssrc_ = read_big_endian<decltype(fields_.ssrc_)>(&buffer_[Ssrc::kOffset]);

        if (fields_.is_extended_) {
            return parse_extension();
        }

        calculate_payload_size();

        return Result::kSuccess;
    }


    [[nodiscard]] Result parse_extension() {
        // extension start after csrc. each csrc is 32 bits (4 bytes) so we skip the
        // amount of csrc_count.
        extension_offset_ = payload_offset_;

        // extension id is the first 16 bits of extension header.
        // extension_header_->id_ = (buffer_[extension_offset] << 8U) |
        // buffer_[extension_offset + 1];
        extension_header_.id_ = read_big_endian<decltype(extension_header_.id_)>(&buffer_[extension_offset_]);

        // extension data length is after the extension id. which is 2 bytes from
        // the offset.
        const std::size_t length_offset = extension_offset_ + 2;

        // extension_header_->length_ = ((buffer_[length_offset] << 8U) |
        // buffer_[length_offset + 1]);
        extension_header_.length_ = read_big_endian<decltype(extension_header_.length_)>(&buffer_[length_offset]);

        // Check if payload offset exceed the size of packet including fixed fields
        // and padding.
        const int number_of_words = extension_header_.length_ * 4;

        // extension data is after the extension length. which is 4 bytes from the
        // extension offset.
        const std::size_t data_offset = length_offset + 2;

        payload_offset_ = data_offset + number_of_words;
        if (payload_offset_ > packet_size_) {
            return Result::kParseExtensionOverflow;
        }

        // extension_header_->data_ = buffer_.subspan(data_offset, number_of_words);
        // extension_header_.data_ = std::span<std::uint8_t>(&buffer_[data_offset],
        // number_of_words);

        calculate_payload_size();


        return Result::kSuccess;
    }
    void extract_csrc() {
        // csrc identifier is 32 bits at offset bit 96 octet: 12 with 4 bytes each.
        // the amount of identifiers is based on ccsrc_count.
        std::size_t current_offset = kFixedRTPSize;

        for (std::size_t idx = 0; idx < fields_.csrc_count_; ++idx) {
            using CsrcElementType = std::remove_cvref_t<decltype(csrc_[idx])>;
            csrc_[idx] = read_big_endian<CsrcElementType>(&buffer_[current_offset]);
            current_offset += kCsrcIdsize;
        }
        fields_.csrc_ = csrc_.data();
    }


public:
    // Getters
    [[nodiscard]] const FixedHeader& get_header() const& noexcept { return fields_; }

    [[nodiscard]] std::span<std::uint32_t> get_csrc() noexcept {
        assert(fields_.csrc_count_ <= csrc_.size());
        return std::span<std::uint32_t>{csrc_.data(), fields_.csrc_count_};
    }

    [[nodiscard]] std::optional<ExtensionHeader> get_extension_header() {
        if (fields_.is_extended_) {
            return extension_header_;
        }

        return {};
    }

    [[nodiscard]] std::uint8_t get_payload_size() const noexcept { return payload_size_; }

    [[nodiscard]] std::uint8_t get_padding_bytes() const noexcept { return padding_bytes_; }

    // Setters
    Result set_padding_bytes(std::uint8_t padding_bytes) {
        if constexpr (ResizableContiguousBuffer<B>) {
            const std::size_t updated_packet_size = packet_size_ - padding_bytes_ + padding_bytes;
            if (updated_packet_size > buffer_.size()) {
                buffer_.resize(updated_packet_size);
            }
        }
        else if (padding_bytes > buffer_.size() - kFixedRTPSize) {
            return Result::kBufferTooSmall;
        }


        bool pad_flag = false;
        if (padding_bytes > 0) {
            pad_flag = true;
            assert(packet_size_ > padding_bytes_ && "packet_size_ is smaller then padding_bytes_");
            packet_size_ -= padding_bytes_;
            packet_size_ += padding_bytes;
            padding_bytes_ = padding_bytes;

            buffer_[packet_size_ - 1] = padding_bytes_;
        }

        buffer_[PaddingBit::kOffset] &= static_cast<std::uint8_t>(~PaddingBit::kMask);
        buffer_[PaddingBit::kOffset] |= (static_cast<std::uint8_t>(pad_flag) << PaddingBit::kShift) & PaddingBit::kMask;

        return Result::kSuccess;
    }

    void set_marker(bool mark) {
        fields_.is_marked_ = mark;
        buffer_[MarkerBit::kOffset] &= static_cast<std::uint8_t>(~MarkerBit::kMask);
        buffer_[MarkerBit::kOffset] |=
            (static_cast<std::uint8_t>(fields_.is_marked_) << MarkerBit::kShift) & MarkerBit::kMask;
    }

    Result set_extension(ExtensionId ext_id) {
        if (!fields_.is_extended_) {
            return set_extension(ExtensionHeader{.id_ = ext_id, .length_ = 0});
        }

        extension_header_.id_ = ext_id;
        write_big_endian(&buffer_[extension_offset_], extension_header_.id_);

        return Result::kSuccess;
    }

    Result set_extension(ExtensionLength length) {
        if (!fields_.is_extended_) {
            return set_extension(ExtensionHeader{.id_ = 0, .length_ = length});
        }


        const std::size_t dst = extension_offset_ + ((length * 4) + 4);
        const std::size_t amount = payload_size_ + padding_bytes_;
        const std::size_t updated_packet_size = dst + amount;

        if (updated_packet_size > buffer_.size()) {
            if constexpr (ResizableContiguousBuffer<B>) {
                buffer_.resize(updated_packet_size);
            }
            else {
                return Result::kBufferTooSmall;
            }
        }

        if (buffer_.size() > dst) {
            assert(dst < buffer_.size());
            memmove(&buffer_[dst], &buffer_[payload_offset_], amount);
        }
        packet_size_ = dst + amount;
        payload_offset_ = dst;
        extension_header_.length_ = length;
        write_big_endian(&buffer_[extension_offset_ + 2], extension_header_.length_);

        return Result::kSuccess;
    }


    Result set_extension(std::optional<ExtensionHeader> header) {
        if (!header.has_value()) {
            toggle_ext_bit(false);
            extension_header_.reset();
            return Result::kSuccess;
        }


        const std::size_t dst = extension_offset_ + header->size_bytes();
        const std::size_t amount = payload_size_ + padding_bytes_;
        const std::size_t updated_packet_size = dst + amount;

        if (updated_packet_size > buffer_.size()) {
            if constexpr (ResizableContiguousBuffer<B>) {
                buffer_.resize(updated_packet_size);
            }
            else {
                return Result::kBufferTooSmall;
            }
        }

        if (buffer_.size() > dst) {
            assert(dst < buffer_.size());
            memmove(&buffer_[dst], &buffer_[payload_offset_], amount);
        }
        packet_size_ = dst + amount;
        payload_offset_ = dst;
        extension_header_ = *header;

        write_big_endian(&buffer_[extension_offset_], extension_header_.id_);
        write_big_endian(&buffer_[extension_offset_ + 2], extension_header_.length_);
        toggle_ext_bit(true);

        return Result::kSuccess;
    }


    StagingCsrcList stage_csrc() {
        if (staged_csrc_list_.empty()) {
            for (auto idx = fields_.csrc_count_; idx < fields_.csrc_count_; ++idx) {
                staged_csrc_list_.emplace_back(csrc_[idx]);
            }
        }
        return StagingCsrcList{*this};
    }

    void set_payload_type(std::uint8_t payload_type) {
        fields_.payload_type_ = payload_type;
        buffer_[PayloadType::kOffset] &= static_cast<std::uint8_t>(~PayloadType::kMask);
        buffer_[PayloadType::kOffset] |= fields_.payload_type_;
    }

    void set_sequence_number(std::uint16_t sequence_number) {
        fields_.sequence_number_ = sequence_number;
        write_big_endian(&buffer_[SequenceNumber::kOffset], fields_.sequence_number_);
    }

    void set_timestamp(std::uint32_t timestamp) {
        fields_.timestamp_ = timestamp;
        write_big_endian(&buffer_[Timestamp::kOffset], fields_.timestamp_);
    }

    void set_ssrc(std::uint32_t ssrc) {
        fields_.ssrc_ = ssrc;
        write_big_endian(&buffer_[Ssrc::kOffset], fields_.ssrc_);
    }


    [[nodiscard]] Result set_payload_size(std::size_t size) {
        const std::size_t end = payload_offset_ + size + padding_bytes_;

        if constexpr (ResizableContiguousBuffer<B>) {
            if (end > packet_size_) {
                buffer_.resize(end);
            }
        }
        else if (end > this->buffer_capacity()) {
            return Result::kBufferTooSmall;
        }

        payload_size_ = size;
        packet_size_ = end;
        // write padding to end of new size
        buffer_[packet_size_ - 1] = padding_bytes_;

        return Result::kSuccess;
    }

    PayloadSpan payload() {
        assert(payload_size_ < packet_size_ && "payload_size bigger then packet_size_ size");
        assert(payload_size_ < buffer_.size() && "payload_size bigger then buffer_ size");
        assert((payload_offset_ >= buffer_.size() && payload_size_ > 0) == false && "payload out of bound buffer_");
        assert((payload_offset_ >= packet_size_ && payload_size_ > 0) == false && "payload out of bound packet_size");
        return std::span<std::uint8_t>(buffer_.data() + payload_offset_, payload_size_);
    }

    ExtensionSpan extension_data() {
        if (!fields_.is_extended_) {
            return {};
        }

        const size_t data_offset = extension_offset_ + 4;

        assert(extension_header_.size_bytes() < packet_size_ && "extension size bigger then packet_size_ size");
        assert(extension_header_.size_bytes() < buffer_.size() && "extension size bigger then buffer_ size");
        assert(
            (data_offset >= buffer_.size() && extension_header_.data_size_bytes() > 0) == false &&
            "extension data out of bound buffer_");
        assert(
            (data_offset >= packet_size_ && extension_header_.data_size_bytes() > 0) == false &&
            "extension data out of bound packet_size");
        return ExtensionSpan(buffer_.data() + data_offset, extension_header_.data_size_bytes());
    }

    [[nodiscard]] PacketBuffer packet() {
        buffer_[Version::kOffset] &= (~Version::kMask);

        // Current RTP version is 2
        static constexpr std::uint8_t kRtpVersionBits = kRtpVersion << Version::kShift;
        buffer_[Version::kOffset] |= kRtpVersionBits;

        assert(packet_size_ <= buffer_.size() && "packet_size bigger then buffer_ size");
        return std::span<std::uint8_t>(buffer_.data(), packet_size_);
    }

    // ContiguousBuffer& buffer() noexcept { return buffer_; }
    B& buffer() noexcept { return buffer_; }

    void reset() noexcept {
        payload_offset_ = 0;
        fields_ = FixedHeader{};
        csrc_ = {0};
        extension_header_.reset();
        padding_bytes_ = 0;
        payload_offset_ = kFixedRTPSize;
        payload_size_ = 0;
    }


private:
    void write_csrc() {
        std::size_t current_csrc_offset = kFixedRTPSize;
        // for (std::size_t idx = 0; idx < fields_.csrc_count_; ++idx) {
        //     assert(current_csrc_offset < buffer_.size() && "csrc data out of bound buffer_");
        //     assert(current_csrc_offset < packet_size_ && "csrc data out of bound buffer_");
        //     write_big_endian(&buffer_[current_csrc_offset], csrc_[idx]);
        //     current_csrc_offset += kCsrcIdsize;
        // }


        for (std::size_t idx = 0; idx < fields_.csrc_count_; ++idx) {
            assert(current_csrc_offset < buffer_.size() && "csrc data out of bound buffer_");
            assert(current_csrc_offset < packet_size_ && "csrc data out of bound buffer_");
            std::uint32_t ssrc = staged_csrc_list_[idx];

            csrc_[idx] = ssrc;
            write_big_endian(&buffer_[current_csrc_offset], ssrc);
            current_csrc_offset += kCsrcIdsize;
        }
    }

    Result set_csrc(std::uint8_t count) {
        if (count > kMaxCsrcIds) {
            return Result::kInvalidCsrcCount;
        }


        // std::size_t csrc_end = kFixedRTPSize + (kCsrcIdsize * csrc_count_);
        const std::size_t dst = kFixedRTPSize + (kCsrcIdsize * count);
        const std::size_t ext_size = current_ext_size_bytes();
        const std::size_t amount = payload_size_ + padding_bytes_ + ext_size;
        const std::size_t updated_packet_size = dst + amount;

        if (updated_packet_size > buffer_.size()) {
            if constexpr (ResizableContiguousBuffer<B>) {
                buffer_.resize(updated_packet_size);
            }
            else {
                return Result::kBufferTooSmall;
            }
        }

        if (buffer_.size() > dst) {
            assert(dst < buffer_.size());
            std::memmove(&buffer_[dst], &buffer_[extension_offset_], amount);
        }

        // packet_size_ = packet_size_ - (static_cast<std::size_t>(csrc_count_) * 4) +
        //                (static_cast<std::size_t>(count) * 4);
        fields_.csrc_count_ = count;
        extension_offset_ = dst;
        payload_offset_ = dst + ext_size;
        packet_size_ = updated_packet_size;


        buffer_[CsrcCount::kOffset] &= (~CsrcCount::kMask);
        buffer_[CsrcCount::kOffset] |= (fields_.csrc_count_ & CsrcCount::kMask);
        write_csrc();

        return Result::kSuccess;
    }


    [[nodiscard]] std::size_t csrc_list_size() const noexcept { return fields_.csrc_count_ * kCsrcIdsize; }
    [[nodiscard]] std::size_t buffer_capacity() const {
        if constexpr (ResizableContiguousBuffer<B>) {
            return buffer_.capacity();
        }

        return buffer_.size();
    }

    void on_parse(std::size_t packet_size) noexcept {
        reset();
        packet_size_ = packet_size;
    }

    void toggle_ext_bit(bool flag) {
        fields_.is_extended_ = flag;
        buffer_[ExtensionBit::kOffset] &= static_cast<std::uint8_t>(~ExtensionBit::kMask);
        buffer_[ExtensionBit::kOffset] |=
            (static_cast<std::uint8_t>(fields_.is_extended_) << ExtensionBit::kShift) & ExtensionBit::kMask;
    }

    [[nodiscard]] std::size_t current_ext_size_bytes() const noexcept {
        if (fields_.is_extended_) {
            return extension_header_.size_bytes();
        }
        return 0;
    }

    void calculate_payload_size() noexcept {
        payload_size_ = packet_size_ - payload_offset_ - static_cast<std::size_t>(padding_bytes_);
        assert(payload_size_ < packet_size_ && "payload_size_ bigger then packet_size_ size");
    }


    static constexpr std::size_t kCsrcIdsize = 4;
    static constexpr std::size_t kMaxCsrcIdsBytes = kCsrcIdsize * kMaxCsrcIds;

    B buffer_{};
    CsrcList csrc_{};
    std::vector<std::uint32_t> staged_csrc_list_;
    FixedHeader fields_{};
    ExtensionHeader extension_header_{};

    std::size_t extension_offset_ = kFixedRTPSize;
    std::size_t payload_offset_ = kFixedRTPSize;
    std::size_t payload_size_ = 0;
    std::size_t packet_size_ = kFixedRTPSize;
    std::uint8_t padding_bytes_ = 0;
};
}; // namespace RtpCpp
