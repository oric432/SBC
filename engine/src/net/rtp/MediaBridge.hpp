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
#include "net/rtp/PjmediaEndpoint.hpp"
#include "protocols/SupportedCodecs.hpp"

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

    // Configures per-leg codec/DTMF-PT info for the call. Opens pjmedia codec
    // sessions (and a resampler, if the two legs' audio clock rates differ)
    // only when the two legs' codec names don't match — passthrough calls pay
    // no pjmedia cost. Must be called before start_bridge_loop() (same
    // not-thread-safe-against-a-running-loop constraint as
    // set_error_handler()) and before any SIP response has committed the
    // call, so a failure here can still be answered with an error response
    // instead of one that's already gone out. `endpoint` is used transiently
    // to open codec/resampler resources — MediaBridge does not retain it.
    //
    // Returns the project's Error type rather than std::error_code (unlike
    // the rest of this class): failures here originate from pjmedia codec/
    // resampler allocation, not socket I/O — see AGENTS.md's error-handling
    // guidance on preferring the project's own error type for that.
    VoidResult configure_legs(PjmediaEndpoint& endpoint, LegCodec leg_a, LegCodec leg_b);

    void start_bridge_loop();

    std::expected<void, std::error_code> close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine
