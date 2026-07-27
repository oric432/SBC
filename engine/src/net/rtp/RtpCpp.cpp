#include "RtpCpp.hpp"

#include <stdexcept>
#include <string>


// --- Start of SmallVector.cpp ---
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
//
// This file implements the SmallVector class.
//
//===----------------------------------------------------------------------===//

// #include "llvm/ADT/Twine.h"
// #include "llvm/Support/MemAlloc.h"
#ifdef LLVM_ENABLE_EXCEPTIONS
    #include <stdexcept>
#endif
using namespace RtpCpp::llvm;
static void* safe_malloc(size_t size) {
    void* p = std::malloc(size);
    if (!p) {
#ifdef __cpp_exceptions
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    return p;
}

static void* safe_realloc(void* ptr, size_t size) {
    void* p = std::realloc(ptr, size);
    if (!p) {
#ifdef __cpp_exceptions
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    return p;
}

// Check that no bytes are wasted and everything is well-aligned.
namespace {
// These structures may cause binary compat warnings on AIX. Suppress the
// warning since we are only using these types for the static assertions below.
#if defined(_AIX)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Waix-compat"
#endif
struct Struct16B {
    alignas(16) void* X;
};
struct Struct32B {
    alignas(32) void* X;
};
#if defined(_AIX)
    #pragma GCC diagnostic pop
#endif
} // namespace
static_assert(
    sizeof(SmallVector<void*, 0>) == sizeof(unsigned) * 2 + sizeof(void*),
    "wasted space in SmallVector size 0");
static_assert(alignof(SmallVector<Struct16B, 0>) >= alignof(Struct16B), "wrong alignment for 16-byte aligned T");
static_assert(alignof(SmallVector<Struct32B, 0>) >= alignof(Struct32B), "wrong alignment for 32-byte aligned T");
static_assert(sizeof(SmallVector<Struct16B, 0>) >= alignof(Struct16B), "missing padding for 16-byte aligned T");
static_assert(sizeof(SmallVector<Struct32B, 0>) >= alignof(Struct32B), "missing padding for 32-byte aligned T");
static_assert(
    sizeof(SmallVector<void*, 1>) == sizeof(unsigned) * 2 + sizeof(void*) * 2,
    "wasted space in SmallVector size 1");

static_assert(
    sizeof(SmallVector<char, 0>) == sizeof(void*) * 2 + sizeof(void*),
    "1 byte elements have word-sized type for size and capacity");

/// Report that MinSize doesn't fit into this vector's size type. Throws
/// std::length_error or calls report_fatal_error.
[[noreturn]] static void report_size_overflow(size_t MinSize, size_t MaxSize);
static void report_size_overflow(size_t MinSize, size_t MaxSize) {
    std::string Reason = "SmallVector unable to grow. Requested capacity (" + std::to_string(MinSize) +
                         ") is larger than maximum value for size type (" + std::to_string(MaxSize) + ")";
#ifdef LLVM_ENABLE_EXCEPTIONS
    throw std::length_error(Reason);
#else
    #ifdef __cpp_exceptions
    throw std::length_error(Reason);
    #else
    std::abort();
    #endif
#endif
}

/// Report that this vector is already at maximum capacity. Throws
/// std::length_error or calls report_fatal_error.
[[noreturn]] static void report_at_maximum_capacity(size_t MaxSize);
static void report_at_maximum_capacity(size_t MaxSize) {
    std::string Reason = "SmallVector capacity unable to grow. Already at maximum size " + std::to_string(MaxSize);
#ifdef LLVM_ENABLE_EXCEPTIONS
    throw std::length_error(Reason);
#else
    #ifdef __cpp_exceptions
    throw std::length_error(Reason);
    #else
    std::abort();
    #endif
#endif
}

// Note: Moving this function into the header may cause performance regression.
template <class Size_T>
static size_t getNewCapacity(size_t MinSize, [[maybe_unused]] size_t TSize, size_t OldCapacity) {
    constexpr size_t MaxSize = std::numeric_limits<Size_T>::max();

    // Ensure we can fit the new capacity.
    // This is only going to be applicable when the capacity is 32 bit.
    if (MinSize > MaxSize) report_size_overflow(MinSize, MaxSize);

    // Ensure we can meet the guarantee of space for at least one more element.
    // The above check alone will not catch the case where grow is called with a
    // default MinSize of 0, but the current capacity cannot be increased.
    // This is only going to be applicable when the capacity is 32 bit.
    if (OldCapacity == MaxSize) report_at_maximum_capacity(MaxSize);

    // In theory 2*capacity can overflow if the capacity is 64 bit, but the
    // original capacity would never be large enough for this to be a problem.
    size_t NewCapacity = 2 * OldCapacity + 1; // Always grow.
    return std::clamp(NewCapacity, MinSize, MaxSize);
}

/// If vector was first created with capacity 0, getFirstEl() points to the
/// memory right after, an area unallocated. If a subsequent allocation,
/// that grows the vector, happens to return the same pointer as getFirstEl(),
/// get a new allocation, otherwise isSmall() will falsely return that no
/// allocation was done (true) and the memory will not be freed in the
/// destructor. If a VSize is given (vector size), also copy that many
/// elements to the new allocation - used if realloca fails to increase
/// space, and happens to allocate precisely at BeginX.
/// This is unlikely to be called often, but resolves a memory leak when the
/// situation does occur.
static void* replaceAllocation(void* NewElts, size_t TSize, size_t NewCapacity, size_t VSize = 0) {
    void* NewEltsReplace = safe_malloc(NewCapacity * TSize);
    if (VSize) memcpy(NewEltsReplace, NewElts, VSize * TSize);
    free(NewElts);
    return NewEltsReplace;
}

// Note: Moving this function into the header may cause performance regression.
template <class Size_T>
void* SmallVectorBase<Size_T>::mallocForGrow(void* FirstEl, size_t MinSize, size_t TSize, size_t& NewCapacity) {
    NewCapacity = getNewCapacity<Size_T>(MinSize, TSize, this->capacity());
    // Even if capacity is not 0 now, if the vector was originally created with
    // capacity 0, it's possible for the malloc to return FirstEl.
    void* NewElts = safe_malloc(NewCapacity * TSize);
    if (NewElts == FirstEl) NewElts = replaceAllocation(NewElts, TSize, NewCapacity);
    return NewElts;
}

// Note: Moving this function into the header may cause performance regression.
template <class Size_T>
void SmallVectorBase<Size_T>::grow_pod(void* FirstEl, size_t MinSize, size_t TSize) {
    size_t NewCapacity = getNewCapacity<Size_T>(MinSize, TSize, this->capacity());
    void* NewElts;
    if (BeginX == FirstEl) {
        NewElts = safe_malloc(NewCapacity * TSize);
        if (NewElts == FirstEl) NewElts = replaceAllocation(NewElts, TSize, NewCapacity);

        // Copy the elements over.  No need to run dtors on PODs.
        memcpy(NewElts, this->BeginX, size() * TSize);
    }
    else {
        // If this wasn't grown from the inline copy, grow the allocated space.
        NewElts = safe_realloc(this->BeginX, NewCapacity * TSize);
        if (NewElts == FirstEl) NewElts = replaceAllocation(NewElts, TSize, NewCapacity, size());
    }

    this->set_allocation_range(NewElts, NewCapacity);
}

template class RtpCpp::llvm::SmallVectorBase<uint32_t>;

// Disable the uint64_t instantiation for 32-bit builds.
// Both uint32_t and uint64_t instantiations are needed for 64-bit builds.
// This instantiation will never be used in 32-bit builds, and will cause
// warnings when sizeof(Size_T) > sizeof(size_t).
#if SIZE_MAX > UINT32_MAX
template class RtpCpp::llvm::SmallVectorBase<uint64_t>;

// Assertions to ensure this #if stays in sync with SmallVectorSizeType.
static_assert(
    sizeof(SmallVectorSizeType<char>) == sizeof(uint64_t),
    "Expected SmallVectorBase<uint64_t> variant to be in use.");
#else
static_assert(
    sizeof(SmallVectorSizeType<char>) == sizeof(uint32_t),
    "Expected SmallVectorBase<uint32_t> variant to be in use.");
#endif
// NOLINTEND
// --- End of SmallVector.cpp ---

// --- Start of BasicRawRtpSender.cpp ---


namespace RtpCpp {

using namespace Detail;

BasicRawRtpSender::BasicRawRtpSender(asio::any_io_executor executor)
    : executor_(std::move(executor)) {}

asio::any_io_executor BasicRawRtpSender::get_executor() {
    return executor_;
}

std::expected<void, std::error_code> BasicRawRtpSender::bind(std::string_view address, unsigned short port) {
    auto endpoint = make_udp_endpoint(address, port);

    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }

    socket_ = std::make_shared<asio::ip::udp::socket>(executor_);
    return open_and_bind_udp_socket(socket_.get(), endpoint.value());
}

std::expected<void, std::error_code> BasicRawRtpSender::bind(std::shared_ptr<asio::ip::udp::socket> socket) {
    if (!socket || !socket->is_open()) {
        return std::unexpected(to_error_code(asio::error::bad_descriptor));
    }
    socket_ = std::move(socket);
    executor_ = socket_->get_executor();
    return {};
}

std::expected<void, std::error_code> BasicRawRtpSender::connect(std::string_view address, unsigned short port) {
    asio_error_code err;
    auto endpoint = make_udp_endpoint(address, port);

    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }

    if (socket_ == nullptr) {
        socket_ = std::make_shared<asio::ip::udp::socket>(executor_);
        (void)socket_->open(endpoint->protocol(), err); // NOLINT
        if (err) {
            return std::unexpected(err);
        }
    }

    (void)socket_->connect(endpoint.value(), err); // NOLINT

    if (err) {
        return std::unexpected(err);
    }
    return {};
}

std::expected<asio::ip::udp::endpoint, std::error_code> BasicRawRtpSender::local_endpoint() const {
    if (socket_ == nullptr || !socket_->is_open()) {
        return std::unexpected(to_error_code(asio::error::bad_descriptor));
    }
    asio_error_code err;
    auto endpoint = socket_->local_endpoint(err);
    if (err) {
        return std::unexpected(err);
    }
    return endpoint;
}

std::expected<void, std::error_code> BasicRawRtpSender::close() {
    if (socket_ == nullptr) {
        return std::unexpected(to_error_code(asio::error::bad_descriptor));
    }

    if (socket_->is_open()) {
        asio_error_code err;
        (void)socket_->close(err); // NOLINT
        if (err) {
            return std::unexpected(err);
        }
    }
    return {};
}

void BasicRawRtpSender::async_send_pkt(
    std::span<const std::uint8_t> raw_packet,
    const asio::ip::udp::endpoint& dst,
    OnRawRtpSend callback) {
    auto exec = asio::get_associated_executor(callback, executor_);
    auto bound_handler = bind_callback_executor(
        exec,
        [self = shared_from_this(),
         cab = std::move(callback)](const std::error_code& err, std::size_t bytes_sent) mutable {
            if (err == to_error_code(asio::error::operation_aborted)) {
                return;
            }
            invoke_callback(std::move(cab), bytes_sent, err);
        });
    execute_async_send_to(raw_packet, dst, std::move(bound_handler));
}

void BasicRawRtpSender::async_send_connected_pkt(std::span<const std::uint8_t> raw_packet, OnRawRtpSend callback) {
    auto exec = asio::get_associated_executor(callback, executor_);
    auto bound_handler = bind_callback_executor(
        exec,
        [self = shared_from_this(),
         cab = std::move(callback)](const std::error_code& err, std::size_t bytes_sent) mutable {
            if (err == to_error_code(asio::error::operation_aborted)) {
                return;
            }
            invoke_callback(std::move(cab), bytes_sent, err);
        });
    execute_async_send(raw_packet, std::move(bound_handler));
}

} // namespace RtpCpp
// --- End of BasicRawRtpSender.cpp ---

// --- Start of ConcurrentRawRtpSender.cpp ---


namespace RtpCpp {

using namespace Detail;

std::shared_ptr<ConcurrentRawRtpSender> ConcurrentRawRtpSender::create(
    const asio::any_io_executor& executor,
    std::size_t max_channel_size) {
    auto sender = std::shared_ptr<ConcurrentRawRtpSender>(new ConcurrentRawRtpSender(executor, max_channel_size));
    sender->start_channel_loop(sender);
    return sender;
}

ConcurrentRawRtpSender::ConcurrentRawRtpSender(const asio::any_io_executor& executor, std::size_t max_channel_size)
    : sender_(std::make_shared<BasicRawRtpSender>(executor))
    , channel_(executor, max_channel_size) {}

asio::any_io_executor ConcurrentRawRtpSender::get_executor() {
    return channel_.get_executor();
}

std::expected<void, std::error_code> ConcurrentRawRtpSender::bind(std::string_view address, unsigned short port) {
    return sender_->bind(address, port);
}

std::expected<void, std::error_code> ConcurrentRawRtpSender::bind(std::shared_ptr<asio::ip::udp::socket> socket) {
    return sender_->bind(std::move(socket));
}

std::expected<void, std::error_code> ConcurrentRawRtpSender::connect(std::string_view address, unsigned short port) {
    return sender_->connect(address, port);
}

std::expected<asio::ip::udp::endpoint, std::error_code> ConcurrentRawRtpSender::local_endpoint() const {
    return sender_->local_endpoint();
}

std::expected<void, std::error_code> ConcurrentRawRtpSender::close() {
    channel_.close();
    return sender_->close();
}

void ConcurrentRawRtpSender::async_send_pkt(
    std::span<const std::uint8_t> raw_packet,
    const asio::ip::udp::endpoint& dst,
    OnRawRtpSend callback) {
    std::vector<std::uint8_t> buffer(raw_packet.begin(), raw_packet.end());
    if (!channel_.try_send(
            std::error_code{},
            ChannelData{.remote_ = dst, .buffer_ = std::move(buffer), .callback_ = std::move(callback)})) {
        post_callback(channel_.get_executor(), {}, 0UL, RtpCpp::to_error_code(asio::error::no_buffer_space));
    }
}

void ConcurrentRawRtpSender::async_send_connected_pkt(std::span<const std::uint8_t> raw_packet, OnRawRtpSend callback) {
    std::vector<std::uint8_t> buffer(raw_packet.begin(), raw_packet.end());
    if (!channel_.try_send(
            std::error_code{},
            ChannelData{.remote_ = std::nullopt, .buffer_ = std::move(buffer), .callback_ = std::move(callback)})) {
        post_callback(channel_.get_executor(), {}, 0UL, to_error_code(asio::error::no_buffer_space));
    }
}

void ConcurrentRawRtpSender::handle_packet(
    std::vector<std::uint8_t> buffer,
    std::optional<asio::ip::udp::endpoint> dst,
    OnRawRtpSend callback) {
    auto exec = asio::get_associated_executor(callback, channel_.get_executor());
    auto bound_handler = bind_callback_executor(
        exec,
        [pkt_buf = buffer, cab = std::move(callback)](std::size_t bytes_sent, const std::error_code& err) mutable {
            if (err == RtpCpp::to_error_code(asio::error::operation_aborted)) {
                return;
            }
            invoke_callback(std::move(cab), bytes_sent, err);
        });

    if (dst) {
        sender_->async_send_pkt(std::move(buffer), dst.value(), std::move(bound_handler));
    }
    else {
        sender_->async_send_connected_pkt(std::move(buffer), std::move(bound_handler));
    }
}

void ConcurrentRawRtpSender::start_channel_loop(std::shared_ptr<ConcurrentRawRtpSender> self) {
    for (;;) {
        const bool rec = channel_.try_receive([this](const RtpCpp::asio_error_code& err, ChannelData data) {
            if (err) {
                if (err != RtpCpp::to_error_code(asio::error::operation_aborted) && data.callback_) {
                    dispatch_callback(channel_.get_executor(), std::move(data.callback_), 0UL, err);
                }
                return;
            }
            handle_packet(std::move(data.buffer_), std::move(data.remote_), std::move(data.callback_));
        });
        if (!rec) {
            break;
        }
    }

    channel_.async_receive([self = std::move(self)](const RtpCpp::asio_error_code& err, ChannelData data) mutable {
        if (err) {
            if (err != RtpCpp::to_error_code(asio::error::operation_aborted) && data.callback_) {
                dispatch_callback(self->channel_.get_executor(), std::move(data.callback_), 0, err);
            }
            if (err != RtpCpp::to_error_code(asio::error::operation_aborted)) {
                auto* raw_this = self.get();
                raw_this->start_channel_loop(std::move(self));
            }
            return;
        }

        self->handle_packet(std::move(data.buffer_), std::move(data.remote_), std::move(data.callback_));
        auto* raw_this = self.get();
        raw_this->start_channel_loop(std::move(self));
    });
}

} // namespace RtpCpp
// --- End of ConcurrentRawRtpSender.cpp ---

// --- Start of RtpReceiver.cpp ---


namespace RtpCpp {

using namespace Detail;

RtpReceiver::RtpReceiver(asio::any_io_executor executor)
    : executor_(std::move(executor)) {}

asio::any_io_executor RtpReceiver::get_executor() {
    return executor_;
}

std::expected<void, std::error_code> RtpReceiver::bind(std::string_view address, unsigned short port) {
    auto endpoint_res = make_udp_endpoint(address, port);
    if (!endpoint_res) {
        return std::unexpected(endpoint_res.error());
    }

    socket_ = std::make_shared<asio::ip::udp::socket>(executor_);

    auto socket_res = open_and_bind_udp_socket(socket_.get(), endpoint_res.value());
    return socket_res;
}

std::expected<void, std::error_code> RtpReceiver::bind(std::shared_ptr<asio::ip::udp::socket> socket) {
    if (socket == nullptr) {
        return std::unexpected(to_error_code(asio::error::bad_descriptor));
    }
    socket_ = std::move(socket);
    executor_ = socket_->get_executor();
    return {};
}

std::expected<void, std::error_code> RtpReceiver::connect(std::string_view address, unsigned short port) {
    auto endpoint_res = make_udp_endpoint(address, port);
    if (!endpoint_res) {
        return std::unexpected(endpoint_res.error());
    }

    if (socket_ == nullptr) {
        socket_ = std::make_shared<asio::ip::udp::socket>(executor_);
        socket_->open(endpoint_res.value().protocol());
    }

    asio_error_code err;
    socket_->connect(endpoint_res.value(), err); // NOLINT
    if (err) {
        return std::unexpected(err);
    }
    return {};
}

std::expected<asio::ip::udp::endpoint, std::error_code> RtpReceiver::local_endpoint() const {
    if (socket_ == nullptr || !socket_->is_open()) {
        return std::unexpected(to_error_code(asio::error::bad_descriptor));
    }
    asio_error_code err;
    auto endpoint = socket_->local_endpoint(err);
    if (err) {
        return std::unexpected(err);
    }
    return endpoint;
}

std::expected<void, std::error_code> RtpReceiver::close() {
    if (socket_ == nullptr) {
        return std::unexpected(to_error_code(asio::error::bad_descriptor));
    }

    if (socket_->is_open()) {
        asio_error_code err;
        socket_->close(err); // NOLINT
        if (err) {
            return std::unexpected(err);
        }
    }

    return {};
}

void RtpReceiver::async_receive_pkt(OnRtpRecvFrom callback) {
    if (socket_ == nullptr || !socket_->is_open()) {
        post_callback(
            executor_,
            std::move(callback),
            RtpPacketView{std::span<std::uint8_t>{}},
            asio::ip::udp::endpoint{},
            to_error_code(asio::error::bad_descriptor));
        return;
    }

    auto buff = asio::buffer(rx_buffer_);
    auto self = shared_from_this();

    auto exec = asio::get_associated_executor(callback, executor_);
    socket_->async_receive_from(
        buff,
        sender_info_,
        bind_callback_executor(
            exec,
            [self, cab = std::move(callback)](const std::error_code& err, std::size_t bytes_received) mutable {
                if (err == to_error_code(asio::error::operation_aborted)) {
                    return;
                }

                RtpPacketView pkt{std::span<std::uint8_t>{self->rx_buffer_.data(), bytes_received}};
                if (err) {
                    invoke_callback(std::move(cab), pkt, self->sender_info_, err);
                }
                else {
                    std::error_code parse_res = pkt.parse(bytes_received);
                    invoke_callback(std::move(cab), pkt, self->sender_info_, parse_res);
                }
            }));
}

void RtpReceiver::async_receive_connected_pkt(OnRtpRecv callback) {
    if (socket_ == nullptr || !socket_->is_open()) {
        post_callback(
            executor_,
            std::move(callback),
            RtpPacketView{std::span<std::uint8_t>{}},
            to_error_code(asio::error::bad_descriptor));
        return;
    }

    auto buff = asio::buffer(rx_buffer_);
    auto self = shared_from_this();

    auto exec = asio::get_associated_executor(callback, executor_);
    socket_->async_receive(
        buff,
        bind_callback_executor(
            exec,
            [self, cab = std::move(callback)](const std::error_code& err, std::size_t bytes_received) mutable {
                if (err == to_error_code(asio::error::operation_aborted)) {
                    return;
                }

                RtpPacketView pkt{std::span<std::uint8_t>{self->rx_buffer_.data(), bytes_received}};
                if (err) {
                    invoke_callback(std::move(cab), pkt, err);
                }
                else {
                    std::error_code parse_res = pkt.parse(bytes_received);
                    invoke_callback(std::move(cab), pkt, parse_res);
                }
            }));
}

} // namespace RtpCpp
// --- End of RtpReceiver.cpp ---

// --- Start of RtpSender.cpp ---

namespace RtpCpp {

using namespace Detail;

RtpSender::RtpSender(std::shared_ptr<IRawRtpSender> raw_sender, RtpSenderConfig config)
    : raw_sender_(std::move(raw_sender))
    , config_(config)
    , current_seq_(Utils::generate_sequence_number()) {}

asio::any_io_executor RtpSender::get_executor() {
    return raw_sender_->get_executor();
}

std::expected<void, std::error_code> RtpSender::bind(std::string_view address, unsigned short port) {
    return raw_sender_->bind(address, port);
}

std::expected<void, std::error_code> RtpSender::bind(std::shared_ptr<asio::ip::udp::socket> socket) {
    return raw_sender_->bind(std::move(socket));
}

std::expected<void, std::error_code> RtpSender::connect(std::string_view address, unsigned short port) {
    return raw_sender_->connect(address, port);
}

std::expected<asio::ip::udp::endpoint, std::error_code> RtpSender::local_endpoint() const {
    return raw_sender_->local_endpoint();
}

std::expected<void, std::error_code> RtpSender::close() {
    return raw_sender_->close();
}

void RtpSender::async_send_pkt(
    std::span<const std::uint8_t> payload,
    std::uint32_t timestamp,
    const asio::ip::udp::endpoint& dst,
    bool marker,
    OnManagedRtpSend callback) {
    std::vector<std::uint8_t> buffer(payload.size() + kFixedRtpHeaderSize);
    RtpPacketView pkt(buffer);

    pkt.set_timestamp(timestamp);
    pkt.set_payload_type(config_.payload_type_);
    pkt.set_ssrc(config_.ssrc_);
    const auto seq = current_seq_.fetch_add(1, std::memory_order_relaxed);
    pkt.set_sequence_number(seq);
    pkt.set_marker(marker);

    const std::error_code err = pkt.set_payload_size(narrow_cast<std::uint16_t>(payload.size()));
    if (err) {
        invoke_callback(std::move(callback), 0, err, 0);
        return;
    }

    std::ranges::copy(payload, pkt.payload().begin());

    const std::span<const uint8_t> pkt_span(buffer);
    auto raw_cb = [buf = std::move(buffer),
                   cab = std::move(callback),
                   seq](std::size_t bytes_sent, const std::error_code& error) mutable {
        invoke_callback(std::move(cab), bytes_sent, error, seq);
    };

    raw_sender_->async_send_pkt(pkt_span, dst, std::move(raw_cb));
}

void RtpSender::async_send_connected_pkt(
    std::span<const std::uint8_t> payload,
    std::uint32_t timestamp,
    bool marker,
    OnManagedRtpSend callback) {
    std::vector<std::uint8_t> buffer(payload.size() + kFixedRtpHeaderSize);
    RtpPacketView pkt(buffer);

    pkt.set_timestamp(timestamp);
    pkt.set_payload_type(config_.payload_type_);
    pkt.set_ssrc(config_.ssrc_);
    const auto seq = current_seq_.fetch_add(1, std::memory_order_relaxed);
    pkt.set_sequence_number(seq);
    pkt.set_marker(marker);

    const std::error_code err = pkt.set_payload_size(narrow_cast<std::uint16_t>(payload.size()));
    if (err) {
        invoke_callback(std::move(callback), 0, err, 0);
        return;
    }

    std::ranges::copy(payload, pkt.payload().begin());

    const std::span<const uint8_t> pkt_span(buffer);
    auto raw_cb = [buf = std::move(buffer),
                   cab = std::move(callback),
                   seq](std::size_t bytes_sent, const std::error_code& error) mutable {
        invoke_callback(std::move(cab), bytes_sent, error, seq);
    };

    raw_sender_->async_send_connected_pkt(pkt_span, std::move(raw_cb));
}

} // namespace RtpCpp
// --- End of RtpSender.cpp ---

// --- Start of RtpSession.cpp ---

namespace RtpCpp {

using namespace Detail;

template <typename RtpSenderType>
RtpSession<RtpSenderType>::RtpSession(std::shared_ptr<RtpSenderType> sender, std::shared_ptr<RtpReceiver> receiver)
    : sender_(std::move(sender))
    , receiver_(std::move(receiver)) {}


template <typename RtpSenderType>
std::expected<void, std::error_code> RtpSession<RtpSenderType>::bind(std::string_view address, unsigned short port) {
    auto shared_socket = std::make_shared<asio::ip::udp::socket>(sender_->get_executor());
    auto endpoint_res = make_udp_endpoint(address, port);


    if (!endpoint_res) {
        return std::unexpected(endpoint_res.error());
    }

    auto skt_res = open_and_bind_udp_socket(shared_socket.get(), endpoint_res.value());
    if (!skt_res) {
        return std::unexpected(skt_res.error());
    }

    if (auto res = sender_->bind(shared_socket); !res) {
        return res;
    }

    if (auto res = receiver_->bind(shared_socket); !res) {
        return res;
    }

    return {};
}


template <typename RtpSenderType>
std::expected<void, std::error_code> RtpSession<RtpSenderType>::close() {
    if (auto res = sender_->close(); !res) {
        return res;
    }

    if (auto res = receiver_->close(); !res) {
        return res;
    }

    return {};
}

template <typename RtpSenderType>
RtpSenderType& RtpSession<RtpSenderType>::sender() const {
    assert(sender_ != nullptr);
    return *sender_;
}
template <typename RtpSenderType>
RtpReceiver& RtpSession<RtpSenderType>::receiver() const {
    assert(receiver_ != nullptr);
    return *receiver_;
}


template class RtpSession<BasicRawRtpSender>;
template class RtpSession<RtpSender>;


} // namespace RtpCpp
// --- End of RtpSession.cpp ---

// --- Start of MediaClock.cpp ---

namespace RtpCpp {

MediaClock::MediaClock(std::uint32_t sample_rate_hz)
    : sample_rate_hz_(sample_rate_hz)
    , random_start_offset_(Utils::generate_timestamp_offset())
    , current_timestamp_(random_start_offset_) {}

std::uint32_t MediaClock::to_rtp_timestamp(std::chrono::nanoseconds hardware_time) const noexcept {
    // Calculate the number of ticks passed, truncating securely to 32-bits (which allows standard
    // RTP wrapping)
    const auto ticks = (hardware_time.count() * sample_rate_hz_) / 1'000'000'000;
    return random_start_offset_ + static_cast<std::uint32_t>(ticks);
}

} // namespace RtpCpp
// --- End of MediaClock.cpp ---
