#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>


// --- Start of RtpCpp.hpp ---

// Core

// --- Start of PayloadTypes.hpp ---

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


constexpr int kMinDynamicRtp = 96;
constexpr int kMaxDynamicRtp = 127;

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


inline bool is_audio_pt(const std::uint8_t payload_type) noexcept {
    using enum AudioPt;
    switch (AudioPt{payload_type}) {
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

inline bool is_video_pt(const std::uint8_t payload_type) noexcept {
    using enum VideoPt;

    switch (VideoPt{payload_type}) {
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

inline std::string_view audio_pt_tostring(const std::uint8_t payload_type) noexcept {
    using enum AudioPt;
    switch (AudioPt{payload_type}) {
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

inline std::string_view video_pt_tostring(const std::uint8_t payload_type) noexcept {
    using enum VideoPt;
    switch (VideoPt{payload_type}) {
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

inline bool is_dynamic_rtp(const std::uint8_t payload_type) noexcept {
    return payload_type >= kMinDynamicRtp && payload_type <= kMaxDynamicRtp;
}

inline bool is_valid_pt(const std::uint8_t payload_type) noexcept {
    return is_audio_pt(payload_type) || is_video_pt(payload_type) || is_dynamic_rtp(payload_type);
}

} // namespace RtpCpp
// --- End of PayloadTypes.hpp ---

// --- Start of RtpPacket.hpp ---


// --- Start of types.hpp ---

namespace RtpCpp {

template <typename T>
concept ContiguousBuffer = std::ranges::contiguous_range<T> && sizeof(std::ranges::range_value_t<T>) == 1;

template <typename C>
concept ResizableContiguousBuffer = ContiguousBuffer<C> && requires(C& buffer, std::size_t n) { buffer.resize(n); };

constexpr std::size_t kMaxRtpPacketSize = 1500;
constexpr std::size_t kFixedRtpHeaderSize = 12;

using RtpBuffer = std::vector<std::uint8_t>;

template <ContiguousBuffer B>
class RtpPacket;
using StaticRtpPacket = RtpPacket<std::array<std::uint8_t, kMaxRtpPacketSize>>;
using DynamicRtpPacket = RtpPacket<std::vector<std::uint8_t>>;
using RtpPacketView = RtpPacket<std::span<std::uint8_t>>;

enum class SocketMode : std::uint8_t { kSingle, kDual };

enum class Result : std::uint8_t {
    kSuccess,
    kError,
    kInvalidPacket,
    kBufferTooSmall,
    kNotImplemented,
    kFixedBufferTooSmall,
    kParseBufferOverflow,
    kParseExtensionOverflow,
    kInvalidHeaderLength,
    kInvalidRtpHeader,
    kInvalidCsrcCount
};


// 2. Define the custom error category
class RtpPacketCatagory : public std::error_category {
public:
    // Unique name identifier for this category
    const char* name() const noexcept override { return "MyAppErrorCategory"; } // NOLINT

    // Map the integer error value to a descriptive string message
    std::string message(int ev) const override { // NOLINT
        switch (static_cast<Result>(ev)) {
        case Result::kSuccess: return "Success";
        case Result::kBufferTooSmall: return "Buffer too small";
        case Result::kParseBufferOverflow: return "Parse overflow buffer length";
        case Result::kInvalidHeaderLength: return "Invalid header length";
        case Result::kInvalidCsrcCount: return "Invalid csrc count";
        case Result::kFixedBufferTooSmall: return "Fixed buffer too small";
        case Result::kParseExtensionOverflow: return "Extension overflow buffer length";
        default: return "Unknown internal error.";
        }
    }
};

// 3. Singleton instance provider
inline const std::error_category& get_rtp_packet_catagory() noexcept {
    const static RtpPacketCatagory instance;
    return instance;
}

// 4. ADL helper function (Must be in the SAME namespace as the enum)
inline std::error_code make_error_code(Result err) noexcept {
    return {static_cast<int>(err), get_rtp_packet_catagory()};
}


} // namespace RtpCpp


// 5. Specialize the std trait (Must be explicitly in the std namespace)
namespace std {
template <>
struct is_error_code_enum<RtpCpp::Result> : true_type {};
} // namespace std
// --- End of types.hpp ---

// --- Start of SmallVector.hpp ---
// NOLINTBEGIN
/*
 * This is a standalone copy of the SmallVector class from LLVM with no
 * dependencies outside standard library.
 *
 * The original code is part of the LLVM Project, under the Apache License v2.0
 * with LLVM Exceptions. See https://llvm.org/LICENSE.txt for license
 * information. SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * This standalone copy was made independent of LLVM and is distributed under
 * the same license.
 */
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the SmallVector class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLVECTOR_H
    #define LLVM_ADT_SMALLVECTOR_H

// #include "llvm/ADT/ADL.h"
// #include "llvm/ADT/DenseMapInfo.h"
// #include "llvm/Support/Compiler.h"

    #ifndef SV_NOINLINE
        #if defined(_MSC_VER)
            #define SV_NOINLINE __declspec(noinline)
        #elif defined(__GNUC__) || defined(__clang__)
            #define SV_NOINLINE __attribute__((noinline))
        #else
            #define SV_NOINLINE
        #endif
    #endif
    #include <algorithm>
    #include <cassert>
    #include <cstddef>
    #include <cstdint>
    #include <cstdlib>
    #include <cstring>
    #include <functional>
    #include <initializer_list>
    #include <iterator>
    #include <limits>
    #include <memory>
    #include <new>
    #include <type_traits>
    #include <utility>

namespace RtpCpp::llvm {

// template <typename T> class ArrayRef;

// template <typename IteratorT> class iterator_range;

template <class Iterator, class Tag>
using HasIteratorTag = std::is_convertible<typename std::iterator_traits<Iterator>::iterator_category, Tag>;

template <class Iterator>
using EnableIfConvertibleToInputIterator = std::enable_if_t<HasIteratorTag<Iterator, std::input_iterator_tag>::value>;

/// This is all the stuff common to all SmallVectors.
///
/// The template parameter specifies the type which should be used to hold the
/// Size and Capacity of the SmallVector, so it can be adjusted.
/// Using 32 bit size is desirable to shrink the size of the SmallVector.
/// Using 64 bit size is desirable for cases like SmallVector<char>, where a
/// 32 bit size would limit the vector to ~4GB. SmallVectors are used for
/// buffering bitcode output - which can exceed 4GB.
template <class Size_T>
class SmallVectorBase {
protected:
    void* BeginX;
    Size_T Size = 0, Capacity;

    /// The maximum value of the Size_T used.
    static constexpr size_t SizeTypeMax() { return std::numeric_limits<Size_T>::max(); }

    SmallVectorBase() = delete;
    SmallVectorBase(void* FirstEl, size_t TotalCapacity)
        : BeginX(FirstEl)
        , Capacity(static_cast<Size_T>(TotalCapacity)) {}

    /// This is a helper for \a grow() that's out of line to reduce code
    /// duplication.  This function will report a fatal error if it can't grow at
    /// least to \p MinSize.
    void* mallocForGrow(void* FirstEl, size_t MinSize, size_t TSize, size_t& NewCapacity);

    /// This is an implementation of the grow() method which only works
    /// on POD-like data types and is out of line to reduce code duplication.
    /// This function will report a fatal error if it cannot increase capacity.
    void grow_pod(void* FirstEl, size_t MinSize, size_t TSize);

public:
    size_t size() const { return Size; }
    size_t capacity() const { return Capacity; }

    [[nodiscard]] bool empty() const { return !Size; }

protected:
    /// Set the array size to \p N, which the current array must have enough
    /// capacity for.
    ///
    /// This does not construct or destroy any elements in the vector.
    void set_size(size_t N) {
        assert(N <= capacity()); // implies no overflow in assignment
        Size = static_cast<Size_T>(N);
    }

    /// Set the array data pointer to \p Begin and capacity to \p N.
    ///
    /// This does not construct or destroy any elements in the vector.
    //  This does not clean up any existing allocation.
    void set_allocation_range(void* Begin, size_t N) {
        assert(N <= SizeTypeMax());
        BeginX = Begin;
        Capacity = static_cast<Size_T>(N);
    }
};

template <class T>
using SmallVectorSizeType = std::conditional_t<sizeof(T) < 4 && sizeof(void*) >= 8, uint64_t, uint32_t>;

/// Figure out the offset of the first element.
template <class T, typename = void>
struct SmallVectorAlignmentAndSize {
    alignas(SmallVectorBase<SmallVectorSizeType<T>>) char Base[sizeof(SmallVectorBase<SmallVectorSizeType<T>>)];
    alignas(T) char FirstEl[sizeof(T)];
};

/// This is the part of SmallVectorTemplateBase which does not depend on whether
/// the type T is a POD. The extra dummy template argument is used by ArrayRef
/// to avoid unnecessarily requiring T to be complete.
template <typename T, typename = void>
class SmallVectorTemplateCommon : public SmallVectorBase<SmallVectorSizeType<T>> {
    using Base = SmallVectorBase<SmallVectorSizeType<T>>;

protected:
    /// Find the address of the first element.  For this pointer math to be valid
    /// with small-size of 0 for T with lots of alignment, it's important that
    /// SmallVectorStorage is properly-aligned even for small-size of 0.
    void* getFirstEl() const {
        return const_cast<void*>(reinterpret_cast<const void*>(
            reinterpret_cast<const char*>(this) + offsetof(SmallVectorAlignmentAndSize<T>, FirstEl)));
    }
    // Space after 'FirstEl' is clobbered, do not add any instance vars after it.

    SmallVectorTemplateCommon(size_t SizeArg)
        : Base(getFirstEl(), SizeArg) {}

    void grow_pod(size_t MinSize, size_t TSize) { Base::grow_pod(getFirstEl(), MinSize, TSize); }

    /// Return true if this is a smallvector which has not had dynamic
    /// memory allocated for it.
    bool isSmall() const { return this->BeginX == getFirstEl(); }

    /// Put this vector in a state of being small.
    void resetToSmall() {
        this->BeginX = getFirstEl();
        this->Size = this->Capacity = 0; // FIXME: Setting Capacity to 0 is suspect.
    }

    /// Return true if V is an internal reference to the given range.
    bool isReferenceToRange(const void* V, const void* First, const void* Last) const {
        // Use std::less to avoid UB.
        std::less<> LessThan;
        return !LessThan(V, First) && LessThan(V, Last);
    }

    /// Return true if V is an internal reference to this vector.
    bool isReferenceToStorage(const void* V) const { return isReferenceToRange(V, this->begin(), this->end()); }

    /// Return true if First and Last form a valid (possibly empty) range in this
    /// vector's storage.
    bool isRangeInStorage(const void* First, const void* Last) const {
        // Use std::less to avoid UB.
        std::less<> LessThan;
        return !LessThan(First, this->begin()) && !LessThan(Last, First) && !LessThan(this->end(), Last);
    }

    /// Return true unless Elt will be invalidated by resizing the vector to
    /// NewSize.
    bool isSafeToReferenceAfterResize(const void* Elt, size_t NewSize) {
        // Past the end.
        if (!isReferenceToStorage(Elt)) [[likely]]
            return true;

        // Return false if Elt will be destroyed by shrinking.
        if (NewSize <= this->size()) return Elt < this->begin() + NewSize;

        // Return false if we need to grow.
        return NewSize <= this->capacity();
    }

    /// Check whether Elt will be invalidated by resizing the vector to NewSize.
    void assertSafeToReferenceAfterResize(const void* Elt, size_t NewSize) {
        assert(
            isSafeToReferenceAfterResize(Elt, NewSize) &&
            "Attempting to reference an element of the vector in an operation "
            "that invalidates it");
        (void)Elt;
        (void)NewSize;
    }

    /// Check whether Elt will be invalidated by increasing the size of the
    /// vector by N.
    void assertSafeToAdd(const void* Elt, size_t N = 1) {
        this->assertSafeToReferenceAfterResize(Elt, this->size() + N);
        (void)Elt;
        (void)N;
    }

    /// Check whether any part of the range will be invalidated by clearing.
    template <class ItTy>
    void assertSafeToReferenceAfterClear(ItTy From, ItTy To) {
        if constexpr (
            std::is_pointer_v<ItTy> &&
            std::is_same_v<std::remove_const_t<std::remove_pointer_t<ItTy>>, std::remove_const_t<T>>) {
            if (From == To) return;
            this->assertSafeToReferenceAfterResize(From, 0);
            this->assertSafeToReferenceAfterResize(To - 1, 0);
        }
        (void)From;
        (void)To;
    }

    /// Check whether any part of the range will be invalidated by growing.
    template <class ItTy>
    void assertSafeToAddRange(ItTy From, ItTy To) {
        if constexpr (std::is_pointer_v<ItTy> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<ItTy>>, T>) {
            if (From == To) return;
            this->assertSafeToAdd(From, To - From);
            this->assertSafeToAdd(To - 1, To - From);
        }
        (void)From;
        (void)To;
    }

    /// Reserve enough space to add one element, and return the updated element
    /// pointer in case it was a reference to the storage.
    template <class U>
    static const T* reserveForParamAndGetAddressImpl(U* This, const T& Elt, size_t N) {
        size_t NewSize = This->size() + N;
        if (NewSize <= This->capacity()) [[likely]]
            return &Elt;

        bool ReferencesStorage = false;
        int64_t Index = -1;
        if (!U::TakesParamByValue) {
            if (This->isReferenceToStorage(&Elt)) [[unlikely]] {
                ReferencesStorage = true;
                Index = &Elt - This->begin();
            }
        }
        This->grow(NewSize);
        return ReferencesStorage ? This->begin() + Index : &Elt;
    }

public:
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using reverse_iterator = std::reverse_iterator<iterator>;

    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;

    using Base::capacity;
    using Base::empty;
    using Base::size;

    // forward iterator creation methods.
    iterator begin() { return static_cast<iterator>(this->BeginX); }
    const_iterator begin() const { return static_cast<const_iterator>(this->BeginX); }
    iterator end() { return begin() + size(); }
    const_iterator end() const { return begin() + size(); }

    // reverse iterator creation methods.
    reverse_iterator rbegin() { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

    size_type size_in_bytes() const { return size() * sizeof(T); }
    size_type max_size() const { return std::min(this->SizeTypeMax(), size_type(-1) / sizeof(T)); }

    size_t capacity_in_bytes() const { return capacity() * sizeof(T); }

    /// Return a pointer to the vector's buffer, even if empty().
    pointer data() { return pointer(begin()); }
    /// Return a pointer to the vector's buffer, even if empty().
    const_pointer data() const { return const_pointer(begin()); }

    reference operator[](size_type idx) {
        assert(idx < size());
        return begin()[idx];
    }
    const_reference operator[](size_type idx) const {
        assert(idx < size());
        return begin()[idx];
    }

    reference front() {
        assert(!empty());
        return begin()[0];
    }
    const_reference front() const {
        assert(!empty());
        return begin()[0];
    }

    reference back() {
        assert(!empty());
        return end()[-1];
    }
    const_reference back() const {
        assert(!empty());
        return end()[-1];
    }
};

/// SmallVectorTemplateBase<TriviallyCopyable = false> - This is where we put
/// method implementations that are designed to work with non-trivial T's.
///
/// We approximate is_trivially_copyable with trivial move/copy construction and
/// trivial destruction. While the standard doesn't specify that you're allowed
/// copy these types with memcpy, there is no way for the type to observe this.
/// This catches the important case of std::pair<POD, POD>, which is not
/// trivially assignable.
template <
    typename T,
    bool = (std::is_trivially_copy_constructible<T>::value) && (std::is_trivially_move_constructible<T>::value) &&
           std::is_trivially_destructible<T>::value>
class SmallVectorTemplateBase : public SmallVectorTemplateCommon<T> {
    friend class SmallVectorTemplateCommon<T>;

protected:
    static constexpr bool TakesParamByValue = false;
    using ValueParamT = const T&;

    SmallVectorTemplateBase(size_t SizeArg)
        : SmallVectorTemplateCommon<T>(SizeArg) {}

    static void destroy_range(T* S, T* E) {
        while (S != E) {
            --E;
            E->~T();
        }
    }

    /// Move the range [I, E) into the uninitialized memory starting with "Dest",
    /// constructing elements as needed.
    template <typename It1, typename It2>
    static void uninitialized_move(It1 I, It1 E, It2 Dest) {
        std::uninitialized_move(I, E, Dest);
    }

    /// Copy the range [I, E) onto the uninitialized memory starting with "Dest",
    /// constructing elements as needed.
    template <typename It1, typename It2>
    static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
        std::uninitialized_copy(I, E, Dest);
    }

    /// Grow the allocated memory (without initializing new elements), doubling
    /// the size of the allocated memory. Guarantees space for at least one more
    /// element, or MinSize more elements if specified.
    void grow(size_t MinSize = 0);

    /// Create a new allocation big enough for \p MinSize and pass back its size
    /// in \p NewCapacity. This is the first section of \a grow().
    T* mallocForGrow(size_t MinSize, size_t& NewCapacity);

    /// Move existing elements over to the new allocation \p NewElts, the middle
    /// section of \a grow().
    void moveElementsForGrow(T* NewElts);

    /// Transfer ownership of the allocation, finishing up \a grow().
    void takeAllocationForGrow(T* NewElts, size_t NewCapacity);

    /// Reserve enough space to add one element, and return the updated element
    /// pointer in case it was a reference to the storage.
    const T* reserveForParamAndGetAddress(const T& Elt, size_t N = 1) {
        return this->reserveForParamAndGetAddressImpl(this, Elt, N);
    }

    /// Reserve enough space to add one element, and return the updated element
    /// pointer in case it was a reference to the storage.
    T* reserveForParamAndGetAddress(T& Elt, size_t N = 1) {
        return const_cast<T*>(this->reserveForParamAndGetAddressImpl(this, Elt, N));
    }

    static T&& forward_value_param(T&& V) { return std::move(V); }
    static const T& forward_value_param(const T& V) { return V; }

    void growAndAssign(size_t NumElts, const T& Elt) {
        // Grow manually in case Elt is an internal reference.
        size_t NewCapacity;
        T* NewElts = mallocForGrow(NumElts, NewCapacity);
        std::uninitialized_fill_n(NewElts, NumElts, Elt);
        this->destroy_range(this->begin(), this->end());
        takeAllocationForGrow(NewElts, NewCapacity);
        this->set_size(NumElts);
    }

    template <typename... ArgTypes>
    T& growAndEmplaceBack(ArgTypes&&... Args) {
        // Grow manually in case one of Args is an internal reference.
        size_t NewCapacity;
        T* NewElts = mallocForGrow(0, NewCapacity);
        ::new (static_cast<void*>(NewElts + this->size())) T(std::forward<ArgTypes>(Args)...);
        moveElementsForGrow(NewElts);
        takeAllocationForGrow(NewElts, NewCapacity);
        this->set_size(this->size() + 1);
        return this->back();
    }

public:
    void push_back(const T& Elt) {
        const T* EltPtr = reserveForParamAndGetAddress(Elt);
        ::new (static_cast<void*>(this->end())) T(*EltPtr);
        this->set_size(this->size() + 1);
    }

    void push_back(T&& Elt) {
        T* EltPtr = reserveForParamAndGetAddress(Elt);
        ::new (static_cast<void*>(this->end())) T(::std::move(*EltPtr));
        this->set_size(this->size() + 1);
    }

    void pop_back() {
        this->set_size(this->size() - 1);
        this->end()->~T();
    }
};

// Define this out-of-line to dissuade the C++ compiler from inlining it.
template <typename T, bool TriviallyCopyable>
void SmallVectorTemplateBase<T, TriviallyCopyable>::grow(size_t MinSize) {
    size_t NewCapacity;
    T* NewElts = mallocForGrow(MinSize, NewCapacity);
    moveElementsForGrow(NewElts);
    takeAllocationForGrow(NewElts, NewCapacity);
}

template <typename T, bool TriviallyCopyable>
T* SmallVectorTemplateBase<T, TriviallyCopyable>::mallocForGrow(size_t MinSize, size_t& NewCapacity) {
    return static_cast<T*>(
        SmallVectorBase<SmallVectorSizeType<T>>::mallocForGrow(this->getFirstEl(), MinSize, sizeof(T), NewCapacity));
}

// Define this out-of-line to dissuade the C++ compiler from inlining it.
template <typename T, bool TriviallyCopyable>
void SmallVectorTemplateBase<T, TriviallyCopyable>::moveElementsForGrow(T* NewElts) {
    // Move the elements over.
    this->uninitialized_move(this->begin(), this->end(), NewElts);

    // Destroy the original elements.
    destroy_range(this->begin(), this->end());
}

// Define this out-of-line to dissuade the C++ compiler from inlining it.
template <typename T, bool TriviallyCopyable>
void SmallVectorTemplateBase<T, TriviallyCopyable>::takeAllocationForGrow(T* NewElts, size_t NewCapacity) {
    // If this wasn't grown from the inline copy, deallocate the old space.
    if (!this->isSmall()) free(this->begin());

    this->set_allocation_range(NewElts, NewCapacity);
}

/// SmallVectorTemplateBase<TriviallyCopyable = true> - This is where we put
/// method implementations that are designed to work with trivially copyable
/// T's. This allows using memcpy in place of copy/move construction and
/// skipping destruction.
template <typename T>
class SmallVectorTemplateBase<T, true> : public SmallVectorTemplateCommon<T> {
    friend class SmallVectorTemplateCommon<T>;

protected:
    /// True if it's cheap enough to take parameters by value. Doing so avoids
    /// overhead related to mitigations for reference invalidation.
    static constexpr bool TakesParamByValue = sizeof(T) <= 2 * sizeof(void*);

    /// Either const T& or T, depending on whether it's cheap enough to take
    /// parameters by value.
    using ValueParamT = std::conditional_t<TakesParamByValue, T, const T&>;

    SmallVectorTemplateBase(size_t SizeArg)
        : SmallVectorTemplateCommon<T>(SizeArg) {}

    // No need to do a destroy loop for POD's.
    static void destroy_range(T*, T*) {}

    /// Move the range [I, E) onto the uninitialized memory
    /// starting with "Dest", constructing elements into it as needed.
    template <typename It1, typename It2>
    static void uninitialized_move(It1 I, It1 E, It2 Dest) {
        // Just do a copy.
        uninitialized_copy(I, E, Dest);
    }

    /// Copy the range [I, E) onto the uninitialized memory
    /// starting with "Dest", constructing elements into it as needed.
    template <typename It1, typename It2>
    static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
        if constexpr (
            std::is_pointer_v<It1> && std::is_pointer_v<It2> &&
            std::is_same_v<std::remove_const_t<std::remove_pointer_t<It1>>, std::remove_pointer_t<It2>>) {
            // Use memcpy for PODs iterated by pointers (which includes SmallVector
            // iterators): std::uninitialized_copy optimizes to memmove, but we can
            // use memcpy here. Note that I and E are iterators and thus might be
            // invalid for memcpy if they are equal.
            if (I != E) std::memcpy(reinterpret_cast<void*>(Dest), I, static_cast<size_t>(E - I) * sizeof(T));
        }
        else {
            // Arbitrary iterator types; just use the basic implementation.
            std::uninitialized_copy(I, E, Dest);
        }
    }

    /// Double the size of the allocated memory, guaranteeing space for at
    /// least one more element or MinSize if specified.
    void grow(size_t MinSize = 0) { this->grow_pod(MinSize, sizeof(T)); }

    /// Reserve enough space to add one element, and return the updated element
    /// pointer in case it was a reference to the storage.
    const T* reserveForParamAndGetAddress(const T& Elt, size_t N = 1) {
        return this->reserveForParamAndGetAddressImpl(this, Elt, N);
    }

    /// Reserve enough space to add one element, and return the updated element
    /// pointer in case it was a reference to the storage.
    T* reserveForParamAndGetAddress(T& Elt, size_t N = 1) {
        return const_cast<T*>(this->reserveForParamAndGetAddressImpl(this, Elt, N));
    }

    /// Copy \p V or return a reference, depending on \a ValueParamT.
    static ValueParamT forward_value_param(ValueParamT V) { return V; }

    void growAndAssign(size_t NumElts, T Elt) {
        // Elt has been copied in case it's an internal reference, side-stepping
        // reference invalidation problems without losing the realloc optimization.
        this->set_size(0);
        this->grow(NumElts);
        std::uninitialized_fill_n(this->begin(), NumElts, Elt);
        this->set_size(NumElts);
    }

    template <typename... ArgTypes>
    T& growAndEmplaceBack(ArgTypes&&... Args) {
        // Use push_back with a copy in case Args has an internal reference,
        // side-stepping reference invalidation problems without losing the realloc
        // optimization.
        push_back(T(std::forward<ArgTypes>(Args)...));
        return this->back();
    }

    // Out-of-line slow path so the inline push_back needs no callee-saved
    // registers or stack frame on its hot path.
    SV_NOINLINE void growAndPushBack(ValueParamT Elt) {
        // Copy in case Elt is an internal reference invalidated by grow.
        T Tmp = Elt;
        this->grow(this->size() + 1);
        std::memcpy(reinterpret_cast<void*>(this->end()), &Tmp, sizeof(T));
        this->set_size(this->size() + 1);
    }

public:
    void push_back(ValueParamT Elt) {
        if (this->size() >= this->capacity()) [[unlikely]]
            return growAndPushBack(Elt);
        std::memcpy(reinterpret_cast<void*>(this->end()), &Elt, sizeof(T));
        this->set_size(this->size() + 1);
    }

    void pop_back() { this->set_size(this->size() - 1); }
};

/// This class consists of common code factored out of the SmallVector class to
/// reduce code duplication based on the SmallVector 'N' template parameter.
template <typename T>
class SmallVectorImpl : public SmallVectorTemplateBase<T> {
    using SuperClass = SmallVectorTemplateBase<T>;

public:
    using iterator = typename SuperClass::iterator;
    using const_iterator = typename SuperClass::const_iterator;
    using reference = typename SuperClass::reference;
    using size_type = typename SuperClass::size_type;

protected:
    using SmallVectorTemplateBase<T>::TakesParamByValue;
    using ValueParamT = typename SuperClass::ValueParamT;

    // Default ctor - Initialize to empty.
    explicit SmallVectorImpl(unsigned N)
        : SmallVectorTemplateBase<T>(N) {}

    void assignRemote(SmallVectorImpl&& RHS) {
        this->destroy_range(this->begin(), this->end());
        if (!this->isSmall()) free(this->begin());
        this->BeginX = RHS.BeginX;
        this->Size = RHS.Size;
        this->Capacity = RHS.Capacity;
        RHS.resetToSmall();
    }

    ~SmallVectorImpl() {
        // Subclass has already destructed this vector's elements.
        // If this wasn't grown from the inline copy, deallocate the old space.
        if (!this->isSmall()) free(this->begin());
    }

public:
    SmallVectorImpl(const SmallVectorImpl&) = delete;

    void clear() {
        this->destroy_range(this->begin(), this->end());
        this->Size = 0;
    }

private:
    // Make set_size() private to avoid misuse in subclasses.
    using SuperClass::set_size;

    template <bool ForOverwrite>
    void resizeImpl(size_type N) {
        if (N == this->size()) return;

        if (N < this->size()) {
            this->truncate(N);
            return;
        }

        this->reserve(N);
        for (auto I = this->end(), E = this->begin() + N; I != E; ++I)
            if (ForOverwrite)
                new (&*I) T;
            else
                new (&*I) T();
        this->set_size(N);
    }

public:
    void resize(size_type N) { resizeImpl<false>(N); }

    /// Like resize, but \ref T is POD, the new values won't be initialized.
    void resize_for_overwrite(size_type N) { resizeImpl<true>(N); }

    /// Like resize, but requires that \p N is less than \a size().
    void truncate(size_type N) {
        assert(this->size() >= N && "Cannot increase size with truncate");
        this->destroy_range(this->begin() + N, this->end());
        this->set_size(N);
    }

    void resize(size_type N, ValueParamT NV) {
        if (N == this->size()) return;

        if (N < this->size()) {
            this->truncate(N);
            return;
        }

        // N > this->size(). Defer to append.
        this->append(N - this->size(), NV);
    }

    void reserve(size_type N) {
        if (this->capacity() < N) this->grow(N);
    }

    void pop_back_n(size_type NumItems) {
        assert(this->size() >= NumItems);
        truncate(this->size() - NumItems);
    }

    [[nodiscard]] T pop_back_val() {
        T Result = ::std::move(this->back());
        this->pop_back();
        return Result;
    }

    void swap(SmallVectorImpl& RHS);

    /// Add the specified range to the end of the SmallVector.
    template <typename ItTy, typename = EnableIfConvertibleToInputIterator<ItTy>>
    void append(ItTy in_start, ItTy in_end) {
        if constexpr (HasIteratorTag<ItTy, std::forward_iterator_tag>::value) {
            this->assertSafeToAddRange(in_start, in_end);
            size_type NumInputs = std::distance(in_start, in_end);
            this->reserve(this->size() + NumInputs);
            this->uninitialized_copy(in_start, in_end, this->end());
            this->set_size(this->size() + NumInputs);
        }
        else {
            // Input iterator, we can't know ahead how many elements we'll add.
            for (; in_start != in_end; ++in_start) this->emplace_back(*in_start);
        }
    }

    /// Append \p NumInputs copies of \p Elt to the end.
    void append(size_type NumInputs, ValueParamT Elt) {
        const T* EltPtr = this->reserveForParamAndGetAddress(Elt, NumInputs);
        std::uninitialized_fill_n(this->end(), NumInputs, *EltPtr);
        this->set_size(this->size() + NumInputs);
    }

    void append(std::initializer_list<T> IL) { append(IL.begin(), IL.end()); }

    void append(const SmallVectorImpl& RHS) { append(RHS.begin(), RHS.end()); }

    void assign(size_type NumElts, ValueParamT Elt) {
        // Note that Elt could be an internal reference.
        if (NumElts > this->capacity()) {
            this->growAndAssign(NumElts, Elt);
            return;
        }

        // Assign over existing elements.
        std::fill_n(this->begin(), std::min(NumElts, this->size()), Elt);
        if (NumElts > this->size())
            std::uninitialized_fill_n(this->end(), NumElts - this->size(), Elt);
        else if (NumElts < this->size())
            this->destroy_range(this->begin() + NumElts, this->end());
        this->set_size(NumElts);
    }

    // FIXME: Consider assigning over existing elements, rather than clearing &
    // re-initializing them - for all assign(...) variants.

    template <typename ItTy, typename = EnableIfConvertibleToInputIterator<ItTy>>
    void assign(ItTy in_start, ItTy in_end) {
        this->assertSafeToReferenceAfterClear(in_start, in_end);
        clear();
        append(in_start, in_end);
    }

    void assign(std::initializer_list<T> IL) {
        clear();
        append(IL);
    }

    void assign(const SmallVectorImpl& RHS) { assign(RHS.begin(), RHS.end()); }

    /* template <typename U,
              typename = std::enable_if_t<std::is_convertible_v<U, T>>>
    void assign(ArrayRef<U> AR) {
      assign(AR.begin(), AR.end());
    } */

    iterator erase(const_iterator CI) {
        // Just cast away constness because this is a non-const member function.
        iterator I = const_cast<iterator>(CI);

        assert(this->isReferenceToStorage(CI) && "Iterator to erase is out of bounds.");

        iterator N = I;
        // Shift all elts down one.
        std::move(I + 1, this->end(), I);
        // Drop the last elt.
        this->pop_back();
        return (N);
    }

    iterator erase(const_iterator CS, const_iterator CE) {
        // Just cast away constness because this is a non-const member function.
        iterator S = const_cast<iterator>(CS);
        iterator E = const_cast<iterator>(CE);

        assert(this->isRangeInStorage(S, E) && "Range to erase is out of bounds.");

        if (S == E) return S;

        iterator N = S;
        // Shift all elts down.
        iterator I = std::move(E, this->end(), S);
        // Drop the last elts.
        this->destroy_range(I, this->end());
        this->set_size(I - this->begin());
        return (N);
    }

private:
    template <class ArgType>
    iterator insert_one_impl(iterator I, ArgType&& Elt) {
        // Callers ensure that ArgType is derived from T.
        static_assert(
            std::is_same<std::remove_const_t<std::remove_reference_t<ArgType>>, T>::value,
            "ArgType must be derived from T!");

        if (I == this->end()) { // Important special case for empty vector.
            this->push_back(::std::forward<ArgType>(Elt));
            return this->end() - 1;
        }

        assert(this->isReferenceToStorage(I) && "Insertion iterator is out of bounds.");

        // Grow if necessary.
        size_t Index = I - this->begin();
        std::remove_reference_t<ArgType>* EltPtr = this->reserveForParamAndGetAddress(Elt);
        I = this->begin() + Index;

        ::new (static_cast<void*>(this->end())) T(::std::move(this->back()));
        // Push everything else over.
        std::move_backward(I, this->end() - 1, this->end());
        this->set_size(this->size() + 1);

        // If we just moved the element we're inserting, be sure to update
        // the reference (never happens if TakesParamByValue).
        static_assert(
            !TakesParamByValue || std::is_same<ArgType, T>::value,
            "ArgType must be 'T' when taking by value!");
        if (!TakesParamByValue && this->isReferenceToRange(EltPtr, I, this->end())) ++EltPtr;

        *I = ::std::forward<ArgType>(*EltPtr);
        return I;
    }

public:
    iterator insert(iterator I, T&& Elt) { return insert_one_impl(I, this->forward_value_param(std::move(Elt))); }

    iterator insert(iterator I, const T& Elt) { return insert_one_impl(I, this->forward_value_param(Elt)); }

    iterator insert(iterator I, size_type NumToInsert, ValueParamT Elt) {
        // Convert iterator to elt# to avoid invalidating iterator when we reserve()
        size_t InsertElt = I - this->begin();

        if (I == this->end()) { // Important special case for empty vector.
            append(NumToInsert, Elt);
            return this->begin() + InsertElt;
        }

        assert(this->isReferenceToStorage(I) && "Insertion iterator is out of bounds.");

        // Ensure there is enough space, and get the (maybe updated) address of
        // Elt.
        const T* EltPtr = this->reserveForParamAndGetAddress(Elt, NumToInsert);

        // Uninvalidate the iterator.
        I = this->begin() + InsertElt;

        // If there are more elements between the insertion point and the end of the
        // range than there are being inserted, we can use a simple approach to
        // insertion.  Since we already reserved space, we know that this won't
        // reallocate the vector.
        if (size_t(this->end() - I) >= NumToInsert) {
            T* OldEnd = this->end();
            append(std::move_iterator<iterator>(this->end() - NumToInsert), std::move_iterator<iterator>(this->end()));

            // Copy the existing elements that get replaced.
            std::move_backward(I, OldEnd - NumToInsert, OldEnd);

            // If we just moved the element we're inserting, be sure to update
            // the reference (never happens if TakesParamByValue).
            if (!TakesParamByValue && I <= EltPtr && EltPtr < this->end()) EltPtr += NumToInsert;

            std::fill_n(I, NumToInsert, *EltPtr);
            return I;
        }

        // Otherwise, we're inserting more elements than exist already, and we're
        // not inserting at the end.

        // Move over the elements that we're about to overwrite.
        T* OldEnd = this->end();
        this->set_size(this->size() + NumToInsert);
        size_t NumOverwritten = OldEnd - I;
        this->uninitialized_move(I, OldEnd, this->end() - NumOverwritten);

        // If we just moved the element we're inserting, be sure to update
        // the reference (never happens if TakesParamByValue).
        if (!TakesParamByValue && I <= EltPtr && EltPtr < this->end()) EltPtr += NumToInsert;

        // Replace the overwritten part.
        std::fill_n(I, NumOverwritten, *EltPtr);

        // Insert the non-overwritten middle part.
        this->uninitialized_fill_n(OldEnd, NumToInsert - NumOverwritten, *EltPtr);
        return I;
    }

    template <typename ItTy, typename = EnableIfConvertibleToInputIterator<ItTy>>
    iterator insert(iterator I, ItTy From, ItTy To) {
        // Convert iterator to elt# to avoid invalidating iterator when we reserve()
        size_t InsertElt = I - this->begin();

        if (I == this->end()) { // Important special case for empty vector.
            append(From, To);
            return this->begin() + InsertElt;
        }

        if constexpr (!HasIteratorTag<ItTy, std::forward_iterator_tag>::value) {
            // For input iterators, we don't know the number of elements to insert.
            size_t OldSize = this->size();
            append(From, To);
            I = this->begin() + InsertElt; // Uninvalidate the iterator.
            std::rotate(I, this->begin() + OldSize, this->end());
            return I;
        }

        assert(this->isReferenceToStorage(I) && "Insertion iterator is out of bounds.");

        // Check that the reserve that follows doesn't invalidate the iterators.
        this->assertSafeToAddRange(From, To);

        size_t NumToInsert = std::distance(From, To);

        // Ensure there is enough space.
        reserve(this->size() + NumToInsert);

        // Uninvalidate the iterator.
        I = this->begin() + InsertElt;

        // If there are more elements between the insertion point and the end of the
        // range than there are being inserted, we can use a simple approach to
        // insertion.  Since we already reserved space, we know that this won't
        // reallocate the vector.
        if (size_t(this->end() - I) >= NumToInsert) {
            T* OldEnd = this->end();
            append(std::move_iterator<iterator>(this->end() - NumToInsert), std::move_iterator<iterator>(this->end()));

            // Copy the existing elements that get replaced.
            std::move_backward(I, OldEnd - NumToInsert, OldEnd);

            std::copy(From, To, I);
            return I;
        }

        // Otherwise, we're inserting more elements than exist already, and we're
        // not inserting at the end.

        // Move over the elements that we're about to overwrite.
        T* OldEnd = this->end();
        this->set_size(this->size() + NumToInsert);
        size_t NumOverwritten = OldEnd - I;
        this->uninitialized_move(I, OldEnd, this->end() - NumOverwritten);

        // Replace the overwritten part.
        for (T* J = I; NumOverwritten > 0; --NumOverwritten) {
            *J = *From;
            ++J;
            ++From;
        }

        // Insert the non-overwritten middle part.
        this->uninitialized_copy(From, To, OldEnd);
        return I;
    }

    void insert(iterator I, std::initializer_list<T> IL) { insert(I, IL.begin(), IL.end()); }

    template <typename... ArgTypes>
    reference emplace_back(ArgTypes&&... Args) {
        if (this->size() >= this->capacity()) [[unlikely]]
            return this->growAndEmplaceBack(std::forward<ArgTypes>(Args)...);

        ::new (static_cast<void*>(this->end())) T(std::forward<ArgTypes>(Args)...);
        this->set_size(this->size() + 1);
        return this->back();
    }

    SmallVectorImpl& operator=(const SmallVectorImpl& RHS);

    SmallVectorImpl& operator=(SmallVectorImpl&& RHS);

    bool operator==(const SmallVectorImpl& RHS) const {
        if (this->size() != RHS.size()) return false;
        return std::equal(this->begin(), this->end(), RHS.begin());
    }
    bool operator!=(const SmallVectorImpl& RHS) const { return !(*this == RHS); }

    bool operator<(const SmallVectorImpl& RHS) const {
        return std::lexicographical_compare(this->begin(), this->end(), RHS.begin(), RHS.end());
    }
    bool operator>(const SmallVectorImpl& RHS) const { return RHS < *this; }
    bool operator<=(const SmallVectorImpl& RHS) const { return !(*this > RHS); }
    bool operator>=(const SmallVectorImpl& RHS) const { return !(*this < RHS); }
};

template <typename T>
void SmallVectorImpl<T>::swap(SmallVectorImpl<T>& RHS) {
    if (this == &RHS) return;

    // We can only avoid copying elements if neither vector is small.
    if (!this->isSmall() && !RHS.isSmall()) {
        std::swap(this->BeginX, RHS.BeginX);
        std::swap(this->Size, RHS.Size);
        std::swap(this->Capacity, RHS.Capacity);
        return;
    }
    this->reserve(RHS.size());
    RHS.reserve(this->size());

    // Swap the shared elements.
    size_t NumShared = this->size();
    if (NumShared > RHS.size()) NumShared = RHS.size();
    for (size_type i = 0; i != NumShared; ++i) std::swap((*this)[i], RHS[i]);

    // Copy over the extra elts.
    if (this->size() > RHS.size()) {
        size_t EltDiff = this->size() - RHS.size();
        this->uninitialized_copy(this->begin() + NumShared, this->end(), RHS.end());
        RHS.set_size(RHS.size() + EltDiff);
        this->destroy_range(this->begin() + NumShared, this->end());
        this->set_size(NumShared);
    }
    else if (RHS.size() > this->size()) {
        size_t EltDiff = RHS.size() - this->size();
        this->uninitialized_copy(RHS.begin() + NumShared, RHS.end(), this->end());
        this->set_size(this->size() + EltDiff);
        this->destroy_range(RHS.begin() + NumShared, RHS.end());
        RHS.set_size(NumShared);
    }
}

template <typename T>
SmallVectorImpl<T>& SmallVectorImpl<T>::operator=(const SmallVectorImpl<T>& RHS) {
    // Avoid self-assignment.
    if (this == &RHS) return *this;

    // If we already have sufficient space, assign the common elements, then
    // destroy any excess.
    size_t RHSSize = RHS.size();
    size_t CurSize = this->size();
    if (CurSize >= RHSSize) {
        // Assign common elements.
        iterator NewEnd;
        if (RHSSize)
            NewEnd = std::copy(RHS.begin(), RHS.begin() + RHSSize, this->begin());
        else
            NewEnd = this->begin();

        // Destroy excess elements.
        this->destroy_range(NewEnd, this->end());

        // Trim.
        this->set_size(RHSSize);
        return *this;
    }

    // If we have to grow to have enough elements, destroy the current elements.
    // This allows us to avoid copying them during the grow.
    // FIXME: don't do this if they're efficiently moveable.
    if (this->capacity() < RHSSize) {
        // Destroy current elements.
        this->clear();
        CurSize = 0;
        this->grow(RHSSize);
    }
    else if (CurSize) {
        // Otherwise, use assignment for the already-constructed elements.
        std::copy(RHS.begin(), RHS.begin() + CurSize, this->begin());
    }

    // Copy construct the new elements in place.
    this->uninitialized_copy(RHS.begin() + CurSize, RHS.end(), this->begin() + CurSize);

    // Set end.
    this->set_size(RHSSize);
    return *this;
}

template <typename T>
SmallVectorImpl<T>& SmallVectorImpl<T>::operator=(SmallVectorImpl<T>&& RHS) {
    // Avoid self-assignment.
    if (this == &RHS) return *this;

    // If the RHS isn't small, clear this vector and then steal its buffer.
    if (!RHS.isSmall()) {
        this->assignRemote(std::move(RHS));
        return *this;
    }

    // If we already have sufficient space, assign the common elements, then
    // destroy any excess.
    size_t RHSSize = RHS.size();
    size_t CurSize = this->size();
    if (CurSize >= RHSSize) {
        // Assign common elements.
        iterator NewEnd = this->begin();
        if (RHSSize) NewEnd = std::move(RHS.begin(), RHS.end(), NewEnd);

        // Destroy excess elements and trim the bounds.
        this->destroy_range(NewEnd, this->end());
        this->set_size(RHSSize);

        // Clear the RHS.
        RHS.clear();

        return *this;
    }

    // If we have to grow to have enough elements, destroy the current elements.
    // This allows us to avoid copying them during the grow.
    // FIXME: this may not actually make any sense if we can efficiently move
    // elements.
    if (this->capacity() < RHSSize) {
        // Destroy current elements.
        this->clear();
        CurSize = 0;
        this->grow(RHSSize);
    }
    else if (CurSize) {
        // Otherwise, use assignment for the already-constructed elements.
        std::move(RHS.begin(), RHS.begin() + CurSize, this->begin());
    }

    // Move-construct the new elements in place.
    this->uninitialized_move(RHS.begin() + CurSize, RHS.end(), this->begin() + CurSize);

    // Set end.
    this->set_size(RHSSize);

    RHS.clear();
    return *this;
}

/// Storage for the SmallVector elements.  This is specialized for the N=0 case
/// to avoid allocating unnecessary storage.
template <typename T, unsigned N>
struct SmallVectorStorage {
    alignas(T) char InlineElts[N * sizeof(T)];
};

/// We need the storage to be properly aligned even for small-size of 0 so that
/// the pointer math in \a SmallVectorTemplateCommon::getFirstEl() is
/// well-defined.
template <typename T>
struct alignas(T) SmallVectorStorage<T, 0> {};

/// Forward declaration of SmallVector so that
/// calculateSmallVectorDefaultInlinedElements can reference
/// `sizeof(SmallVector<T, 0>)`.
template <typename T, unsigned N>
class SmallVector;

/// Helper class for calculating the default number of inline elements for
/// `SmallVector<T>`.
///
/// This should be migrated to a constexpr function when our minimum
/// compiler support is enough for multi-statement constexpr functions.
template <typename T>
struct CalculateSmallVectorDefaultInlinedElements {
    // Parameter controlling the default number of inlined elements
    // for `SmallVector<T>`.
    //
    // The default number of inlined elements ensures that
    // 1. There is at least one inlined element.
    // 2. `sizeof(SmallVector<T>) <= kPreferredSmallVectorSizeof` unless
    // it contradicts 1.
    static constexpr size_t kPreferredSmallVectorSizeof = 64;

    // static_assert that sizeof(T) is not "too big".
    //
    // Because our policy guarantees at least one inlined element, it is possible
    // for an arbitrarily large inlined element to allocate an arbitrarily large
    // amount of inline storage. We generally consider it an antipattern for a
    // SmallVector to allocate an excessive amount of inline storage, so we want
    // to call attention to these cases and make sure that users are making an
    // intentional decision if they request a lot of inline storage.
    //
    // We want this assertion to trigger in pathological cases, but otherwise
    // not be too easy to hit. To accomplish that, the cutoff is actually somewhat
    // larger than kPreferredSmallVectorSizeof (otherwise,
    // `SmallVector<SmallVector<T>>` would be one easy way to trip it, and that
    // pattern seems useful in practice).
    //
    // One wrinkle is that this assertion is in theory non-portable, since
    // sizeof(T) is in general platform-dependent. However, we don't expect this
    // to be much of an issue, because most LLVM development happens on 64-bit
    // hosts, and therefore sizeof(T) is expected to *decrease* when compiled for
    // 32-bit hosts, dodging the issue. The reverse situation, where development
    // happens on a 32-bit host and then fails due to sizeof(T) *increasing* on a
    // 64-bit host, is expected to be very rare.
    static_assert(
        sizeof(T) <= 256,
        "You are trying to use a default number of inlined elements for "
        "`SmallVector<T>` but `sizeof(T)` is really big! Please use an "
        "explicit number of inlined elements with `SmallVector<T, N>` to make "
        "sure you really want that much inline storage.");

    // Discount the size of the header itself when calculating the maximum inline
    // bytes.
    static constexpr size_t PreferredInlineBytes = kPreferredSmallVectorSizeof - sizeof(SmallVector<T, 0>);
    static constexpr size_t NumElementsThatFit = PreferredInlineBytes / sizeof(T);
    static constexpr size_t value = NumElementsThatFit == 0 ? 1 : NumElementsThatFit;
};

/// This is a 'vector' (really, a variable-sized array), optimized
/// for the case when the array is small.  It contains some number of elements
/// in-place, which allows it to avoid heap allocation when the actual number of
/// elements is below that threshold.  This allows normal "small" cases to be
/// fast without losing generality for large inputs.
///
/// \note
/// In the absence of a well-motivated choice for the number of inlined
/// elements \p N, it is recommended to use \c SmallVector<T> (that is,
/// omitting the \p N). This will choose a default number of inlined elements
/// reasonable for allocation on the stack (for example, trying to keep \c
/// sizeof(SmallVector<T>) around 64 bytes).
///
/// \warning This does not attempt to be exception safe.
///
/// \see https://llvm.org/docs/ProgrammersManual.html#llvm-adt-smallvector-h
template <typename T, unsigned N = CalculateSmallVectorDefaultInlinedElements<T>::value>
class SmallVector : public SmallVectorImpl<T>, SmallVectorStorage<T, N> {
public:
    SmallVector()
        : SmallVectorImpl<T>(N) {}

    ~SmallVector() {
        // Destroy the constructed elements in the vector.
        this->destroy_range(this->begin(), this->end());
    }

    explicit SmallVector(size_t SizeArg)
        : SmallVectorImpl<T>(N) {
        this->resize(SizeArg);
    }

    SmallVector(size_t SizeArg, const T& Value)
        : SmallVectorImpl<T>(N) {
        this->assign(SizeArg, Value);
    }

    template <typename ItTy, typename = EnableIfConvertibleToInputIterator<ItTy>>
    SmallVector(ItTy S, ItTy E)
        : SmallVectorImpl<T>(N) {
        this->append(S, E);
    }

    /* template <typename RangeTy>
    explicit SmallVector(const iterator_range<RangeTy> &R)
        : SmallVectorImpl<T>(N) {
      this->append(R.begin(), R.end());
    } */

    SmallVector(std::initializer_list<T> IL)
        : SmallVectorImpl<T>(N) {
        this->append(IL);
    }

    /* template <typename U,
              typename = std::enable_if_t<std::is_convertible_v<U, T>>>
    explicit SmallVector(ArrayRef<U> A) : SmallVectorImpl<T>(N) {
      this->append(A.begin(), A.end());
    } */

    SmallVector(const SmallVector& RHS)
        : SmallVectorImpl<T>(N) {
        if (!RHS.empty()) SmallVectorImpl<T>::operator=(RHS);
    }

    SmallVector& operator=(const SmallVector& RHS) {
        SmallVectorImpl<T>::operator=(RHS);
        return *this;
    }

    SmallVector(SmallVector&& RHS)
        : SmallVectorImpl<T>(N) {
        if (!RHS.empty()) SmallVectorImpl<T>::operator=(::std::move(RHS));
    }

    SmallVector(SmallVectorImpl<T>&& RHS)
        : SmallVectorImpl<T>(N) {
        if (!RHS.empty()) SmallVectorImpl<T>::operator=(::std::move(RHS));
    }

    SmallVector& operator=(SmallVector&& RHS) {
        if (N) {
            SmallVectorImpl<T>::operator=(::std::move(RHS));
            return *this;
        }
        // SmallVectorImpl<T>::operator= does not leverage N==0. Optimize the
        // case.
        if (this == &RHS) return *this;
        if (RHS.empty()) {
            this->destroy_range(this->begin(), this->end());
            this->Size = 0;
        }
        else {
            this->assignRemote(std::move(RHS));
        }
        return *this;
    }

    SmallVector& operator=(SmallVectorImpl<T>&& RHS) {
        SmallVectorImpl<T>::operator=(::std::move(RHS));
        return *this;
    }

    SmallVector& operator=(std::initializer_list<T> IL) {
        this->assign(IL);
        return *this;
    }
};

template <typename T, unsigned N>
inline size_t capacity_in_bytes(const SmallVector<T, N>& X) {
    return X.capacity_in_bytes();
}

template <typename RangeType>
using ValueTypeFromRangeType = std::remove_reference_t<decltype(*std::begin(std::declval<RangeType&>()))>;

/// Given a range of type R, iterate the entire range and return a
/// SmallVector with elements of the vector.  This is useful, for example,
/// when you want to iterate a range and then sort the results.
template <unsigned Size, typename R>
SmallVector<ValueTypeFromRangeType<R>, Size> to_vector(R&& Range) {
    return SmallVector<ValueTypeFromRangeType<R>, Size>(std::begin(Range), std::end(Range));
}
template <typename R>
SmallVector<ValueTypeFromRangeType<R>> to_vector(R&& Range) {
    return SmallVector<ValueTypeFromRangeType<R>>(std::begin(Range), std::end(Range));
}

template <typename Out, unsigned Size, typename R>
SmallVector<Out, Size> to_vector_of(R&& Range) {
    return SmallVector<Out, Size>(std::begin(Range), std::end(Range));
}

template <typename Out, typename R>
SmallVector<Out> to_vector_of(R&& Range) {
    return SmallVector<Out>(std::begin(Range), std::end(Range));
}

// Explicit instantiations
extern template class SmallVectorBase<uint32_t>;
    #if SIZE_MAX > UINT32_MAX
extern template class SmallVectorBase<uint64_t>;
    #endif

// DenseMapInfo removed for standalone version.

} // end namespace RtpCpp::llvm

namespace std {

/// Implement std::swap in terms of SmallVector swap.
template <typename T>
inline void swap(RtpCpp::llvm::SmallVectorImpl<T>& LHS, RtpCpp::llvm::SmallVectorImpl<T>& RHS) {
    LHS.swap(RHS);
}

/// Implement std::swap in terms of SmallVector swap.
template <typename T, unsigned N>
inline void swap(RtpCpp::llvm::SmallVector<T, N>& LHS, RtpCpp::llvm::SmallVector<T, N>& RHS) {
    LHS.swap(RHS);
}

} // end namespace std

#endif // LLVM_ADT_SMALLVECTOR_H
// NOLINTEND
// --- End of SmallVector.hpp ---

// --- Start of casts.hpp ---


namespace RtpCpp::Detail {

template <std::integral To, std::integral From>
[[nodiscard]] constexpr To narrow_cast(From value) noexcept {
    assert(std::in_range<To>(value) && "narrow_cast failed: value out of bounds for target type");
    return static_cast<To>(value);
}

} // namespace RtpCpp::Detail
// --- End of casts.hpp ---

// --- Start of endianness.hpp ---


// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

namespace RtpCpp::Detail {


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
        // return static_cast<uint16_t>((buffer[0] << 8U) | buffer[1]);
        return static_cast<uint16_t>((static_cast<uint32_t>(buffer[0]) << 8U) | static_cast<uint32_t>(buffer[1]));
    }

    if constexpr (::std::same_as<T, uint32_t>) {
        return (static_cast<uint32_t>(buffer[0]) << 24U) | (static_cast<uint32_t>(buffer[1]) << 16U) |
               (static_cast<uint32_t>(buffer[2]) << 8U) | static_cast<uint32_t>(buffer[3]);
    }
}

} // namespace RtpCpp::Detail
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers)// --- End of endianness.hpp ---


// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)

namespace RtpCpp {


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
};

enum ExtensionId : std::uint16_t; // NOLINT(cppcoreguidelines-use-enum-class)
enum ExtensionLength : std::uint16_t; // NOLINT(cppcoreguidelines-use-enum-class)

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


template <ContiguousBuffer B>
class RtpPacket {
private:
    // RTP header has minimum size of 12 bytes.
    static constexpr std::size_t kFixedRTPSize = 12;
    static constexpr std::size_t kMaxCsrcIds = 15;
    static constexpr std::uint8_t kRtpVersion = 2;
    static constexpr std::size_t kMaxFixedPktSize = kFixedRTPSize + (kMaxCsrcIds * 4);

    // using ElementT = typename B::value_type;
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


public:
    using CsrcList = llvm::SmallVector<std::uint32_t, kMaxCsrcIds>;
    RtpPacket()
        requires ResizableContiguousBuffer<B>
        : buffer_(kFixedRTPSize) {
        write_rtp_version();
    }
    explicit RtpPacket(std::size_t reserve_buffer_size)
        requires ResizableContiguousBuffer<B>
        : buffer_(reserve_buffer_size + kFixedRTPSize) {
        write_rtp_version();
    }

    // explicit RtpPacket(const B& buffer)
    //     : buffer_(buffer) {};

    explicit RtpPacket(B buffer)
        : buffer_(std::move(buffer)) {
        write_rtp_version();
    };


    RtpPacket()
        requires(std::is_same_v<B, std::span<std::uint8_t>>)
    = default;

    RtpPacket()
        requires(!ResizableContiguousBuffer<B> && !std::is_same_v<B, std::span<std::uint8_t>>)
    {
        write_rtp_version();
    }

    RtpPacket(const RtpPacket&) = default;
    RtpPacket& operator=(const RtpPacket&) = default;
    RtpPacket(RtpPacket&&) = default;
    RtpPacket& operator=(RtpPacket&&) = default;
    ~RtpPacket() = default;


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
        const auto version = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(buffer_[Version::kOffset]) & Version::kMask) >> Version::kShift);


        // RFC 3550 RTP version is 2.
        if (version != kRtpVersion) {
            return Result::kInvalidRtpHeader;
        }

        // Padding bit is the 2 bit in octet 0
        const bool is_padded =
            (((static_cast<std::uint32_t>(buffer_[PaddingBit::kOffset]) & PaddingBit::kMask) >> PaddingBit::kShift) !=
             0U);
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
        fields_.is_extended_ =
            (((static_cast<std::uint32_t>(buffer_[ExtensionBit::kOffset]) & ExtensionBit::kMask) >>
              ExtensionBit::kShift) != 0U);


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
            Detail::read_big_endian<decltype(fields_.sequence_number_)>(&buffer_[SequenceNumber::kOffset]);

        // timestamp is 32 bits at offset 32 octet: 4, 5, 6, 7,
        fields_.timestamp_ = Detail::read_big_endian<decltype(fields_.timestamp_)>(&buffer_[Timestamp::kOffset]);

        // ssrc identifier is 32 bits at offset 64 octet: 8, 9 ,10 ,11
        fields_.ssrc_ = Detail::read_big_endian<decltype(fields_.ssrc_)>(&buffer_[Ssrc::kOffset]);

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
        extension_header_.id_ = Detail::read_big_endian<decltype(extension_header_.id_)>(&buffer_[extension_offset_]);

        // extension data length is after the extension id. which is 2 bytes from
        // the offset.
        const std::size_t length_offset = extension_offset_ + 2;

        // extension_header_->length_ = ((buffer_[length_offset] << 8U) |
        // buffer_[length_offset + 1]);
        extension_header_.length_ =
            Detail::read_big_endian<decltype(extension_header_.length_)>(&buffer_[length_offset]);

        // Check if payload offset exceed the size of packet including fixed fields
        // and padding.
        const std::size_t number_of_words = static_cast<std::size_t>(extension_header_.length_) * 4U;

        // extension data is after the extension length. which is 4 bytes from the
        // extension offset.
        const std::size_t data_offset = length_offset + 2;

        payload_offset_ = Detail::narrow_cast<std::uint16_t>(data_offset + number_of_words);
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
        // the amount of identifiers is based on csrc_count.
        csrc_.clear();
        std::size_t current_offset = kFixedRTPSize;

        for (std::size_t idx = 0; idx < fields_.csrc_count_; ++idx) {
            csrc_.push_back(Detail::read_big_endian<std::uint32_t>(&buffer_[current_offset]));
            current_offset += kCsrcIdsize;
        }
    }


public:
    // Getters
    [[nodiscard]] const FixedHeader& get_header() const& noexcept { return fields_; }

    [[nodiscard]] std::span<const std::uint32_t> get_csrc() const noexcept {
        return std::span<const std::uint32_t>{csrc_.data(), csrc_.size()};
    }

    [[nodiscard]] CsrcList& csrc_list() noexcept { return csrc_; }
    [[nodiscard]] const CsrcList& csrc_list() const noexcept { return csrc_; }

    [[nodiscard]] Result commit_csrc() {
        if (csrc_.size() > kMaxCsrcIds) {
            return Result::kInvalidCsrcCount;
        }
        return set_csrc(static_cast<std::uint8_t>(csrc_.size()));
    }

    [[nodiscard]] std::optional<ExtensionHeader> get_extension_header() const noexcept {
        if (fields_.is_extended_) {
            return extension_header_;
        }

        return {};
    }

    [[nodiscard]] std::uint16_t get_payload_size() const noexcept { return payload_size_; }

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

        buffer_[PaddingBit::kOffset] = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(buffer_[PaddingBit::kOffset]) &
             ~static_cast<std::uint32_t>(PaddingBit::kMask)) |
            ((static_cast<std::uint32_t>(pad_flag) << PaddingBit::kShift) &
             static_cast<std::uint32_t>(PaddingBit::kMask)));

        return Result::kSuccess;
    }

    void set_marker(bool mark) {
        fields_.is_marked_ = mark;
        buffer_[MarkerBit::kOffset] = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(buffer_[MarkerBit::kOffset]) & ~static_cast<std::uint32_t>(MarkerBit::kMask)) |
            ((static_cast<std::uint32_t>(fields_.is_marked_) << MarkerBit::kShift) &
             static_cast<std::uint32_t>(MarkerBit::kMask)));
    }

    Result set_extension(ExtensionId ext_id) {
        if (!fields_.is_extended_) {
            return set_extension(ExtensionHeader{.id_ = ext_id, .length_ = 0});
        }

        extension_header_.id_ = ext_id;
        Detail::write_big_endian(&buffer_[extension_offset_], extension_header_.id_);

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
        packet_size_ = Detail::narrow_cast<std::uint16_t>(dst + amount);
        payload_offset_ = Detail::narrow_cast<std::uint16_t>(dst);
        extension_header_.length_ = length;
        Detail::write_big_endian(&buffer_[extension_offset_ + 2], extension_header_.length_);

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
        packet_size_ = Detail::narrow_cast<std::uint16_t>(dst + amount);
        payload_offset_ = Detail::narrow_cast<std::uint16_t>(dst);
        extension_header_ = *header;

        Detail::write_big_endian(&buffer_[extension_offset_], extension_header_.id_);
        Detail::write_big_endian(&buffer_[extension_offset_ + 2], extension_header_.length_);
        toggle_ext_bit(true);

        return Result::kSuccess;
    }


    void set_payload_type(std::uint8_t payload_type) {
        fields_.payload_type_ = payload_type;
        buffer_[PayloadType::kOffset] &= static_cast<std::uint8_t>(~PayloadType::kMask);
        buffer_[PayloadType::kOffset] |= fields_.payload_type_;
    }

    void set_sequence_number(std::uint16_t sequence_number) {
        fields_.sequence_number_ = sequence_number;
        Detail::write_big_endian(&buffer_[SequenceNumber::kOffset], fields_.sequence_number_);
    }

    void set_timestamp(std::uint32_t timestamp) {
        fields_.timestamp_ = timestamp;
        Detail::write_big_endian(&buffer_[Timestamp::kOffset], fields_.timestamp_);
    }

    void set_ssrc(std::uint32_t ssrc) {
        fields_.ssrc_ = ssrc;
        Detail::write_big_endian(&buffer_[Ssrc::kOffset], fields_.ssrc_);
    }


    [[nodiscard]] Result set_payload_size(std::uint16_t size) {
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
        packet_size_ = Detail::narrow_cast<std::uint16_t>(end);
        // write padding to end of new size
        buffer_[packet_size_ - 1] = padding_bytes_;

        return Result::kSuccess;
    }

    PayloadSpan payload() noexcept {
        assert(payload_size_ < packet_size_ && "payload_size bigger then packet_size_ size");
        assert(payload_size_ < buffer_.size() && "payload_size bigger then buffer_ size");
        assert((payload_offset_ >= buffer_.size() && payload_size_ > 0) == false && "payload out of bound buffer_");
        assert((payload_offset_ >= packet_size_ && payload_size_ > 0) == false && "payload out of bound packet_size");
        return std::span<std::uint8_t>(std::next(buffer_.data(), payload_offset_), payload_size_);
    }

    [[nodiscard]] std::span<const std::uint8_t> payload() const noexcept {
        assert(payload_size_ < packet_size_ && "payload_size bigger then packet_size_ size");
        assert(payload_size_ < buffer_.size() && "payload_size bigger then buffer_ size");
        assert((payload_offset_ >= buffer_.size() && payload_size_ > 0) == false && "payload out of bound buffer_");
        assert((payload_offset_ >= packet_size_ && payload_size_ > 0) == false && "payload out of bound packet_size");
        return std::span<const std::uint8_t>(std::next(buffer_.data(), payload_offset_), payload_size_);
    }

    ExtensionSpan extension_data() noexcept {
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
        return ExtensionSpan(std::next(buffer_.data(), data_offset), extension_header_.data_size_bytes());
    }

    [[nodiscard]] std::span<const std::uint8_t> extension_data() const noexcept {
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
        return std::span<const std::uint8_t>(
            std::next(buffer_.data(), data_offset),
            extension_header_.data_size_bytes());
    }

    [[nodiscard]] PacketBuffer packet() noexcept {
        assert(packet_size_ <= buffer_.size() && "packet_size bigger then buffer_ size");
        return std::span<std::uint8_t>(buffer_.data(), packet_size_);
    }

    [[nodiscard]] std::span<const std::uint8_t> packet() const noexcept {
        assert(packet_size_ <= buffer_.size() && "packet_size bigger then buffer_ size");
        return std::span<const std::uint8_t>(buffer_.data(), packet_size_);
    }

    [[nodiscard]] B& buffer() noexcept { return buffer_; }
    [[nodiscard]] const B& buffer() const noexcept { return buffer_; }

    void reset() noexcept {
        payload_offset_ = 0;
        fields_ = FixedHeader{};
        csrc_.clear();
        extension_header_.reset();
        padding_bytes_ = 0;
        payload_offset_ = kFixedRTPSize;
        payload_size_ = 0;
    }


private:
    void write_rtp_version() {
        // TODO: Consider how to propagate an error to the caller if buffer size is less than
        // kFixedRTPSize (12 bytes) so that an invalid/short buffer is not silently ignored.
        if (buffer_.size() < kFixedRTPSize) {
            return;
        }

        if constexpr (!std::is_const_v<std::remove_pointer_t<decltype(buffer_.data())>>) {
            buffer_[Version::kOffset] &= static_cast<std::uint8_t>(~Version::kMask);
            buffer_[Version::kOffset] |= static_cast<std::uint8_t>((2U << Version::kShift) & Version::kMask);
        }
    }

    void write_csrc() {
        std::size_t current_csrc_offset = kFixedRTPSize;

        for (std::size_t idx = 0; idx < fields_.csrc_count_; ++idx) {
            assert(current_csrc_offset < buffer_.size() && "csrc data out of bound buffer_");
            assert(current_csrc_offset < packet_size_ && "csrc data out of bound buffer_");
            std::uint32_t ssrc = csrc_[idx];

            Detail::write_big_endian(&buffer_[current_csrc_offset], ssrc);
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
        extension_offset_ = Detail::narrow_cast<std::uint16_t>(dst);
        payload_offset_ = Detail::narrow_cast<std::uint16_t>(dst + ext_size);
        packet_size_ = Detail::narrow_cast<std::uint16_t>(updated_packet_size);


        buffer_[CsrcCount::kOffset] = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(buffer_[CsrcCount::kOffset]) & ~static_cast<std::uint32_t>(CsrcCount::kMask)) |
            (static_cast<std::uint32_t>(fields_.csrc_count_) & static_cast<std::uint32_t>(CsrcCount::kMask)));
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
        packet_size_ = Detail::narrow_cast<std::uint16_t>(packet_size);
    }

    void toggle_ext_bit(bool flag) {
        fields_.is_extended_ = flag;
        buffer_[ExtensionBit::kOffset] = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(buffer_[ExtensionBit::kOffset]) &
             ~static_cast<std::uint32_t>(ExtensionBit::kMask)) |
            ((static_cast<std::uint32_t>(fields_.is_extended_) << ExtensionBit::kShift) &
             static_cast<std::uint32_t>(ExtensionBit::kMask)));
    }

    [[nodiscard]] std::size_t current_ext_size_bytes() const noexcept {
        if (fields_.is_extended_) {
            return extension_header_.size_bytes();
        }
        return 0;
    }

    void calculate_payload_size() noexcept {
        payload_size_ = Detail::narrow_cast<std::uint16_t>(packet_size_ - payload_offset_ - padding_bytes_);
        assert(payload_size_ < packet_size_ && "payload_size_ bigger then packet_size_ size");
    }


    static constexpr std::size_t kCsrcIdsize = 4;
    static constexpr std::size_t kMaxCsrcIdsBytes = kCsrcIdsize * kMaxCsrcIds;

    B buffer_{};
    CsrcList csrc_;

    std::uint16_t extension_offset_ = kFixedRTPSize;
    std::uint16_t payload_offset_ = kFixedRTPSize;
    std::uint16_t payload_size_ = 0;
    std::uint16_t packet_size_ = kFixedRTPSize;

    FixedHeader fields_{};
    ExtensionHeader extension_header_{};
    std::uint8_t padding_bytes_ = 0;
};
}; // namespace RtpCpp

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)
// --- End of RtpPacket.hpp ---

// --- Start of io_types.hpp ---


// --- Start of asio.hpp ---

#ifdef RTPCPP_USE_BOOST_ASIO
    #include <boost/asio.hpp>
    #include <boost/asio/experimental/concurrent_channel.hpp>
#else
    #include <asio.hpp>
    #include <asio/experimental/concurrent_channel.hpp>
#endif

namespace RtpCpp {
#ifdef RTPCPP_USE_BOOST_ASIO
namespace asio = boost::asio;
using asio_error_code = boost::system::error_code;
#else
namespace asio = ::asio; // NOLINT
using asio_error_code = std::error_code; // NOLINT
#endif

inline std::error_code to_error_code(asio::error::basic_errors err) {
    return make_error_code(err);
}
} // namespace RtpCpp
// --- End of asio.hpp ---

namespace RtpCpp {

using OnRawRtpSend = asio::any_completion_handler<void(std::size_t, const std::error_code&)>;
using OnManagedRtpSend = asio::any_completion_handler<void(std::size_t, const std::error_code&, std::uint16_t)>;

using OnRtpRecv = asio::any_completion_handler<void(const RtpPacketView&, const std::error_code&)>;
using OnRtpRecvFrom =
    asio::any_completion_handler<void(const RtpPacketView&, const asio::ip::udp::endpoint&, const std::error_code&)>;

} // namespace RtpCpp
// --- End of io_types.hpp ---

// IO

// --- Start of BasicRawRtpSender.hpp ---


// --- Start of IRawRtpSender.hpp ---


// --- Start of IRtpTransport.hpp ---


namespace RtpCpp {

class IRtpTransport {
public:
    IRtpTransport() = default;
    virtual ~IRtpTransport() = default;

    IRtpTransport(const IRtpTransport&) = delete;
    IRtpTransport& operator=(const IRtpTransport&) = delete;
    IRtpTransport(IRtpTransport&&) = delete;
    IRtpTransport& operator=(IRtpTransport&&) = delete;
    virtual std::expected<void, std::error_code> bind(std::string_view address, unsigned short port) = 0;


    virtual asio::any_io_executor get_executor() = 0;

    virtual std::expected<void, std::error_code> bind(std::shared_ptr<asio::ip::udp::socket> socket) = 0;

    virtual std::expected<void, std::error_code> connect(std::string_view address, unsigned short port) = 0;


    [[nodiscard]] virtual std::expected<asio::ip::udp::endpoint, std::error_code> local_endpoint() const = 0;
    virtual std::expected<void, std::error_code> close() = 0;
};

} // namespace RtpCpp
// --- End of IRtpTransport.hpp ---

namespace RtpCpp {

class IRawRtpSender : public virtual IRtpTransport {
public:
    IRawRtpSender() = default;
    ~IRawRtpSender() override = default;

    IRawRtpSender(const IRawRtpSender&) = delete;
    IRawRtpSender& operator=(const IRawRtpSender&) = delete;
    IRawRtpSender(IRawRtpSender&&) = delete;
    IRawRtpSender& operator=(IRawRtpSender&&) = delete;

    virtual void async_send_pkt(
        std::span<const std::uint8_t> raw_packet,
        const asio::ip::udp::endpoint& dst,
        OnRawRtpSend callback) = 0;

    virtual void async_send_connected_pkt(std::span<const std::uint8_t> raw_packet, OnRawRtpSend callback) = 0;
};

} // namespace RtpCpp
// --- End of IRawRtpSender.hpp ---


namespace RtpCpp {

class BasicRawRtpSender : public std::enable_shared_from_this<BasicRawRtpSender>, public IRawRtpSender {
public:
    explicit BasicRawRtpSender(asio::any_io_executor executor);

    std::expected<void, std::error_code> bind(std::string_view address = "0.0.0.0", unsigned short port = 0) override;

    std::expected<void, std::error_code> bind(std::shared_ptr<asio::ip::udp::socket> socket) override;

    std::expected<void, std::error_code> connect(std::string_view address, unsigned short port) override;

    std::expected<asio::ip::udp::endpoint, std::error_code> local_endpoint() const override;

    std::expected<void, std::error_code> close() override;

    asio::any_io_executor get_executor() override;

    void async_send_pkt(
        std::span<const std::uint8_t> raw_packet,
        const asio::ip::udp::endpoint& dst,
        OnRawRtpSend callback) override;

    void async_send_connected_pkt(std::span<const std::uint8_t> raw_packet, OnRawRtpSend callback) override;

private:
    template <typename ConcreteHandler>
    void execute_async_send_to(
        std::span<const std::uint8_t> pkt,
        const asio::ip::udp::endpoint& dst,
        ConcreteHandler&& wrapped_handler) {
        socket_->async_send_to(asio::buffer(pkt), dst, std::forward<ConcreteHandler>(wrapped_handler));
    }

    template <typename ConcreteHandler>
    void execute_async_send(std::span<const std::uint8_t> pkt, ConcreteHandler&& wrapped_handler) {
        socket_->async_send(asio::buffer(pkt), std::forward<ConcreteHandler>(wrapped_handler));
    }

    asio::any_io_executor executor_;
    std::shared_ptr<asio::ip::udp::socket> socket_;
};

} // namespace RtpCpp
// --- End of BasicRawRtpSender.hpp ---

// --- Start of ConcurrentRawRtpSender.hpp ---


namespace RtpCpp {

class ConcurrentRawRtpSender : public std::enable_shared_from_this<ConcurrentRawRtpSender>, public IRawRtpSender {
public:
    static std::shared_ptr<ConcurrentRawRtpSender> create(
        const asio::any_io_executor& executor,
        std::size_t max_channel_size);

    std::expected<void, std::error_code> bind(std::string_view address = "0.0.0.0", unsigned short port = 0) override;

    std::expected<void, std::error_code> bind(std::shared_ptr<asio::ip::udp::socket> socket) override;

    std::expected<void, std::error_code> connect(std::string_view address, unsigned short port) override;

    std::expected<asio::ip::udp::endpoint, std::error_code> local_endpoint() const override;

    std::expected<void, std::error_code> close() override;

    asio::any_io_executor get_executor() override;

    void async_send_pkt(
        std::span<const std::uint8_t> raw_packet,
        const asio::ip::udp::endpoint& dst,
        OnRawRtpSend callback) override;

    void async_send_connected_pkt(std::span<const std::uint8_t> raw_packet, OnRawRtpSend callback) override;

private:
    ConcurrentRawRtpSender(const asio::any_io_executor& executor, std::size_t max_channel_size);

    struct ChannelData {
        std::optional<asio::ip::udp::endpoint> remote_;
        std::vector<std::uint8_t> buffer_;
        OnRawRtpSend callback_;
    };

    void
    handle_packet(std::vector<std::uint8_t> buffer, std::optional<asio::ip::udp::endpoint> dst, OnRawRtpSend callback);

    void start_channel_loop(std::shared_ptr<ConcurrentRawRtpSender> self);

    std::shared_ptr<IRawRtpSender> sender_;
    asio::experimental::concurrent_channel<void(RtpCpp::asio_error_code, ChannelData)> channel_;
};

} // namespace RtpCpp
// --- End of ConcurrentRawRtpSender.hpp ---

// --- Start of IRtpSender.hpp ---


namespace RtpCpp {

class IRtpSender : public virtual IRtpTransport {
public:
    IRtpSender() = default;
    ~IRtpSender() override = default;

    IRtpSender(const IRtpSender&) = delete;
    IRtpSender& operator=(const IRtpSender&) = delete;
    IRtpSender(IRtpSender&&) = delete;
    IRtpSender& operator=(IRtpSender&&) = delete;

    virtual void async_send_pkt(
        std::span<const std::uint8_t> payload,
        std::uint32_t timestamp,
        const asio::ip::udp::endpoint& dst,
        bool marker,
        OnManagedRtpSend callback) = 0;

    virtual void async_send_connected_pkt(
        std::span<const std::uint8_t> payload,
        std::uint32_t timestamp,
        bool marker,
        OnManagedRtpSend callback) = 0;
};

} // namespace RtpCpp
// --- End of IRtpSender.hpp ---

// --- Start of RtpReceiver.hpp ---


namespace RtpCpp {

class RtpReceiver : public std::enable_shared_from_this<RtpReceiver>, public IRtpTransport {
public:
    explicit RtpReceiver(asio::any_io_executor executor);

    std::expected<void, std::error_code> bind(std::string_view address, unsigned short port) override;

    std::expected<void, std::error_code> bind(std::shared_ptr<asio::ip::udp::socket> socket) override;

    std::expected<void, std::error_code> connect(std::string_view address, unsigned short port) override;

    std::expected<asio::ip::udp::endpoint, std::error_code> local_endpoint() const override;

    std::expected<void, std::error_code> close() override;

    asio::any_io_executor get_executor() override;

    void async_receive_pkt(OnRtpRecvFrom callback);

    void async_receive_connected_pkt(OnRtpRecv callback);

private:
    std::array<std::uint8_t, kMaxRtpPacketSize> rx_buffer_{};
    asio::any_io_executor executor_;
    std::shared_ptr<asio::ip::udp::socket> socket_;
    asio::ip::udp::endpoint sender_info_;
};

} // namespace RtpCpp
// --- End of RtpReceiver.hpp ---

// --- Start of RtpSender.hpp ---


// --- Start of rtp_utils.hpp ---


namespace RtpCpp {

namespace Detail {
template <typename T>
T generate_random_id() {
    constexpr T kMin = std::numeric_limits<T>::min();
    constexpr T kMax = std::numeric_limits<T>::max();

    std::random_device rand_dev;
    std::mt19937 generator(rand_dev());
    std::uniform_int_distribution<T> distr(kMin, kMax);

    return distr(generator);
}


}; // namespace Detail

namespace Utils {

inline std::uint32_t generate_ssrc() {
    return Detail::generate_random_id<std::uint32_t>();
}

inline std::uint16_t generate_sequence_number() {
    return Detail::generate_random_id<std::uint16_t>();
}

inline std::uint32_t generate_timestamp_offset() {
    return Detail::generate_random_id<std::uint32_t>();
}


} // namespace Utils

} // namespace RtpCpp
// --- End of rtp_utils.hpp ---

namespace RtpCpp {

struct RtpSenderConfig {
    std::uint8_t payload_type_;
    std::uint32_t ssrc_ = Utils::generate_ssrc();
};

class RtpSender : public IRtpSender {
public:
    RtpSender(std::shared_ptr<IRawRtpSender> raw_sender, RtpSenderConfig config);

    std::expected<void, std::error_code> bind(std::string_view address = "0.0.0.0", unsigned short port = 0) override;

    std::expected<void, std::error_code> bind(std::shared_ptr<asio::ip::udp::socket> socket) override;

    std::expected<void, std::error_code> connect(std::string_view address, unsigned short port) override;

    [[nodiscard]] std::expected<asio::ip::udp::endpoint, std::error_code> local_endpoint() const override;

    std::expected<void, std::error_code> close() override;

    asio::any_io_executor get_executor() override;

    void async_send_pkt(
        std::span<const std::uint8_t> payload,
        std::uint32_t timestamp,
        const asio::ip::udp::endpoint& dst,
        bool marker,
        OnManagedRtpSend callback) override;

    void async_send_connected_pkt(
        std::span<const std::uint8_t> payload,
        std::uint32_t timestamp,
        bool marker,
        OnManagedRtpSend callback) override;

private:
    std::shared_ptr<IRawRtpSender> raw_sender_;
    RtpSenderConfig config_;
    std::atomic<std::uint16_t> current_seq_;
};

} // namespace RtpCpp
// --- End of RtpSender.hpp ---

// --- Start of RtpSession.hpp ---


namespace RtpCpp {

template <typename RtpSenderType>
class RtpSession {
public:
    RtpSession(std::shared_ptr<RtpSenderType> sender, std::shared_ptr<RtpReceiver> receiver);


    [[nodiscard]] RtpSenderType& sender() const;

    std::expected<void, std::error_code> bind(std::string_view address = "0.0.0.0", unsigned short port = 0);

    [[nodiscard]] RtpReceiver& receiver() const;


    std::expected<void, std::error_code> close();

private:
    std::shared_ptr<RtpSenderType> sender_;
    std::shared_ptr<RtpReceiver> receiver_;
};


inline RtpSession<BasicRawRtpSender> make_raw_rtp_session(const asio::any_io_executor& executor) {
    return {std::make_shared<BasicRawRtpSender>(executor), std::make_shared<RtpReceiver>(executor)};
}

inline RtpSession<RtpSender> make_rtp_session(const asio::any_io_executor& executor, RtpSenderConfig sender_config) {
    return {
        std::make_shared<RtpSender>(std::make_shared<BasicRawRtpSender>(executor), sender_config),
        std::make_shared<RtpReceiver>(executor)};
}


} // namespace RtpCpp
// --- End of RtpSession.hpp ---

// Utils

// --- Start of MediaClock.hpp ---


namespace RtpCpp {

class MediaClock {
public:
    explicit MediaClock(std::uint32_t sample_rate_hz);

    // Maps an absolute hardware timestamp to an RTP timestamp
    [[nodiscard]] std::uint32_t to_rtp_timestamp(std::chrono::nanoseconds hardware_time) const noexcept;

    // Advances the internal clock state by a relative duration
    template <typename Rep, typename Period>
    std::uint32_t advance(std::chrono::duration<Rep, Period> duration) noexcept {
        const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
        const auto ticks = (duration_ns.count() * sample_rate_hz_) / 1'000'000'000;
        current_timestamp_ += static_cast<std::uint32_t>(ticks);
        return current_timestamp_;
    }

    [[nodiscard]] std::uint32_t current_timestamp() const noexcept { return current_timestamp_; }

    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_hz_; }

private:
    std::uint32_t sample_rate_hz_;
    std::uint32_t random_start_offset_;
    std::uint32_t current_timestamp_;
};

} // namespace RtpCpp
// --- End of MediaClock.hpp ---

// --- Start of asio_utils.hpp ---


namespace RtpCpp::Detail {

template <typename Executor, typename Handler, typename... Args>
void dispatch_callback(const Executor& fallback_executor, Handler&& handler, Args&&... args) {
    if (!handler) {
        return;
    }
    auto exec = asio::get_associated_executor(handler, fallback_executor);
    asio::dispatch(exec, [handler = std::forward<Handler>(handler), ... args = std::forward<Args>(args)]() mutable {
        std::move(handler)(std::forward<Args>(args)...);
    });
}

template <typename Executor, typename... Args>
void dispatch_callback(const Executor& /*fallback_executor*/, std::nullptr_t, Args&&... /*unused*/) {
    // Empty callback passed, do nothing
}

template <typename Handler, typename... Args>
void invoke_callback(Handler&& handler, Args&&... args) {
    if (!handler) {
        return;
    }
    std::forward<Handler>(handler)(std::forward<Args>(args)...);
}

template <typename... Args>
void invoke_callback(std::nullptr_t, Args&&... /*unused*/) {
    // Empty callback passed, do nothing
}

template <typename Executor, typename Handler, typename... Args>
void post_callback(const Executor& fallback_executor, Handler&& handler, Args&&... args) {
    if (!handler) {
        return;
    }
    auto exec = asio::get_associated_executor(handler, fallback_executor);
    asio::post(
        fallback_executor,
        [exec, handler = std::forward<Handler>(handler), ... args = std::forward<Args>(args)]() mutable {
            std::move(handler)(std::move(args)...);
        });
}

template <typename Executor, typename... Args>
void post_callback(const Executor& /*fallback_executor*/, std::nullptr_t, Args&&... /*unused*/) {
    // Empty callback passed, do nothing
}

template <typename DefaultExecutor, typename Func>
auto bind_callback_executor(const DefaultExecutor& executor, Func&& fnc) {
    return asio::bind_executor(executor, std::forward<Func>(fnc));
}


inline std::expected<asio::ip::udp::endpoint, std::error_code> make_udp_endpoint(
    std::string_view address = "0.0.0.0",
    unsigned short port = 0) {
    RtpCpp::asio_error_code err;
    auto addr = asio::ip::make_address(std::string(address), err);
    if (err) {
        return std::unexpected(err);
    }
    return asio::ip::udp::endpoint{addr, port};
}


inline std::expected<void, std::error_code> open_and_bind_udp_socket(
    asio::ip::udp::socket* skt,
    const asio::ip::udp::endpoint& endpoint) {
    RtpCpp::asio_error_code err;

    if (skt == nullptr) {
        return std::unexpected(RtpCpp::to_error_code(asio::error::bad_descriptor));
    }

    (void)skt->open(endpoint.protocol(), err); // NOLINT

    if (err) {
        return std::unexpected(err);
    }

    (void)skt->bind(endpoint, err); // NOLINT

    if (err) {
        return std::unexpected(err);
    }

    return {};
}

} // namespace RtpCpp::Detail
// --- End of asio_utils.hpp ---
// --- End of RtpCpp.hpp ---
