#pragma once

#include <concepts>
#include <cstdint>

namespace RtpCpp {


#if defined(_MSC_VER)
    #include <stdlib.h> // for _byteswap_* functions
#endif

static constexpr uint16_t swap_ushort(uint16_t bytes) {
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap16(bytes);
#elif defined(_MSC_VER)
    return _byteswap_ushort(bytes);
#elif __cplusplus >= 202302L
    return std::byteswap(bytes)
#else
    return (bytes >> 8) | (bytes << 8);
#endif
}

static constexpr uint32_t swap_ulong(uint32_t bytes) {
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap32(bytes);
#elif defined(_MSC_VER)
    return _byteswap_ulong(bytes);
#elif __cplusplus >= 202302L
    return std::byteswap(bytes)
#else
    return ((bytes & 0xFF'00'00'00) >> 24) | ((bytes & 0x00'FF'00'00) >> 8) | ((bytes & 0x00'00'FF'00) << 8) |
           ((bytes & 0x00'00'00'FF) << 24);
#endif
}

static constexpr uint64_t swap_uint64(uint64_t bytes) {
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap64(bytes);
#elif defined(_MSC_VER)
    return _byteswap_uint64(bytes);
#elif __cplusplus >= 202302L
    return std::byteswap(bytes)
#else
    return ((bytes & 0xFF'00'00'00'00'00'00'00ULL) >> 56) | ((bytes & 0x00'FF'00'00'00'00'00'00ULL) >> 40) |
           ((bytes & 0x00'00'FF'00'00'00'00'00ULL) >> 24) | ((bytes & 0x00'00'00'FF'00'00'00'00ULL) >> 8) |
           ((bytes & 0x00'00'00'00'FF'00'00'00ULL) << 8) | ((bytes & 0x00'00'00'00'00'FF'00'00ULL) << 24) |
           ((bytes & 0x00'00'00'00'00'00'FF'00ULL) << 40);
#endif
}

template <typename T>
concept UintType = ::std::same_as<T, uint16_t> || ::std::same_as<T, uint32_t> || ::std::same_as<T, uint64_t>;


template <::std::same_as<uint8_t> T>
void write_big_endian(uint8_t* buffer, T data) {
    *buffer = data;
}

template <::std::same_as<uint16_t> T>
void write_big_endian(uint8_t* buffer, const T data) {
    buffer[0] = static_cast<uint8_t>((data & 0xFF'00U) >> 8);
    buffer[1] = static_cast<uint8_t>(data & 0x00'FFU);
}

template <::std::same_as<uint32_t> T>
void write_big_endian(::std::uint8_t* buffer, const T data) {
    buffer[0] = (data & 0xFF'00'00'00U) >> 24;
    buffer[1] = (data & 0x00'FF'00'00U) >> 16;
    buffer[2] = (data & 0x00'00'FF'00U) >> 8;
    buffer[3] = (data & 0x00'00'00'FFU);
}

template <::std::same_as<uint64_t> T>
void write_big_endian(uint8_t* buffer, const T data) {
    buffer[0] = (data & 0xFF'00'00'00'00'00'00'00ULL) >> 56;
    buffer[1] = (data & 0x00'FF'00'00'00'00'00'00ULL) >> 48;
    buffer[2] = (data & 0x00'00'FF'00'00'00'00'00ULL) >> 40;
    buffer[3] = (data & 0x00'00'00'FF'00'00'00'00ULL) >> 32;
    buffer[4] = (data & 0x00'00'00'00'FF'00'00'00ULL) >> 24;
    buffer[5] = (data & 0x00'00'00'00'00'FF'00'00ULL) >> 16;
    buffer[6] = (data & 0x00'00'00'00'00'00'FF'00ULL) >> 16;
    buffer[7] = (data & 0x00'00'00'00'00'00'00'FFULL) >> 8;
}

template <UintType T>
T read_big_endian(const uint8_t* buffer) {
    if constexpr (::std::same_as<T, uint16_t>) {
        return (buffer[0] << 8U) | buffer[1];
    }

    if constexpr (::std::same_as<T, uint32_t>) {
        return ((buffer[0] << 24U) | (buffer[1] << 16U) | (buffer[2] << 8U) | buffer[3]);
    }
}

} // namespace RtpCpp