#pragma once

#include <cstdint>
#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/udp.hpp>

#include "core/utils/error.hpp"
#include "net/rtp/pjmedia_endpoint.hpp"
#include "protocols/supported_codecs.hpp"

namespace SbcEngine {

// Identifies which of the bridge's two RTP sockets an error occurred on.
enum class RelayLeg : std::uint8_t { kLegA, kLegB };

// Identifies which operation on that socket failed.
enum class RelayOp : std::uint8_t { kReceive, kSend };

std::string_view to_string(RelayLeg leg);
std::string_view to_string(RelayOp operation);

// Invoked from the relay loop whenever a transport error occurs relaying RTP
// for one leg. `operation_aborted` (the loop tearing down via close()) is
// filtered out before this fires, so every invocation is a real error worth
// acting on. Runs on the bridge's own executor, synchronously inside the
// relay loop's completion handler — keep it fast and non-blocking.
using MediaBridgeErrorHandler = std::function<void(RelayLeg leg, RelayOp operation, std::error_code error)>;

// One leg's negotiated audio codec plus (if the leg's offer/answer included
// one) its telephone-event payload type — both decided independently per
// leg by SDP negotiation (issue #172), and both potentially different from
// the other leg's.
struct LegCodec {
    Protocols::SupportedCodec audio_;
    std::optional<std::uint8_t> dtmf_pt_;
};

class MediaBridge : public std::enable_shared_from_this<MediaBridge> {
public:
    explicit MediaBridge(const boost::asio::any_io_executor& executor);
    ~MediaBridge();

    MediaBridge(const MediaBridge&) = delete;
    MediaBridge& operator=(const MediaBridge&) = delete;
    MediaBridge(MediaBridge&&) = delete;
    MediaBridge& operator=(MediaBridge&&) = delete;

    std::expected<unsigned short, std::error_code> bind_leg_a();
    std::expected<unsigned short, std::error_code> bind_leg_b();

    std::expected<unsigned short, std::error_code> leg_a_port() const;
    std::expected<unsigned short, std::error_code> leg_b_port() const;

    void set_remote_leg_a(const std::string& addr, unsigned short port);
    void set_remote_leg_b(const std::string& addr, unsigned short port);

    // Same effect as set_remote_leg_a/b, but safe to call after
    // start_bridge_loop() is already running: marshals the write onto this
    // bridge's own executor so it can never race the relay loop's concurrent
    // reads of the remote endpoint. Use this (not set_remote_leg_a/b) to
    // retarget an already-active call's media path, e.g. for a re-INVITE
    // that only changes one leg's own RTP address/port. Also arms the
    // opposite leg's receive loop if it was never armed because this
    // endpoint was still unknown when start_bridge_loop() first ran.
    void retarget_remote_leg_a(std::string addr, unsigned short port);
    void retarget_remote_leg_b(std::string addr, unsigned short port);

    // Empty until the corresponding set_remote_leg_* call — i.e. before the
    // peer's SDP has been parsed.
    [[nodiscard]] std::optional<boost::asio::ip::udp::endpoint> remote_leg_a() const;
    [[nodiscard]] std::optional<boost::asio::ip::udp::endpoint> remote_leg_b() const;

    // Not thread-safe against a running relay loop: call before start_bridge_loop(),
    // or otherwise marshal onto the bridge's own executor — the relay loop reads
    // this handler without synchronization on the assumption it is set up front.
    void set_error_handler(MediaBridgeErrorHandler handler);

    // The timestamp is written by the RTP executor and may safely be read by
    // the SIP thread. It is the default time point until the relay is started.
    [[nodiscard]] std::chrono::steady_clock::time_point last_packet_time() const;

    // Opens codec/resampler resources synchronously (only when the codec names
    // differ, so a failure precedes any SIP response), then posts the swap onto
    // the bridge's executor — safe on a running relay loop. `endpoint` isn't retained.
    VoidResult configure_legs(PjmediaEndpoint& endpoint, LegCodec leg_a, LegCodec leg_b);

    // Arms the relay loop for each leg whose remote endpoint is already set.
    // Idempotent: a second call (e.g. the 200 OK following an early-media
    // 183 that already armed it) is a no-op rather than double-listening on
    // either socket, which would corrupt in-flight payloads (see #214).
    void start_bridge_loop();

    // Marks the bridge as closing and closes both legs' sockets. Posted onto
    // this bridge's own executor, the same reason as retarget_remote_leg_a/b
    // and configure_legs: closing a socket directly from a foreign thread
    // races the relay loop, and a send/receive completion that re-arms
    // listen() afterward on the now-closed socket fails with a synchronous
    // bad_descriptor (not operation_aborted) and spins forever re-arming
    // itself (see issue #208). Safe to call even if neither leg was ever
    // bound, and safe to call more than once.
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine
