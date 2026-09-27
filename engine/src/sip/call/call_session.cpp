#include "call_session.hpp"

#include <chrono>
#include <format>

#include "sip/call/call_manager.hpp"
#include "sip/call/i_call_event_sink.hpp"
#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
constexpr pj_size_t kPoolInitial = 4096;
constexpr pj_size_t kPoolIncrement = 4096;

std::string iso8601_now() {
    return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
}
} // namespace

CallSession::CallSession(
    std::string call_id,
    PjContext* ctx,
    CallManager* call_manager,
    const EngineStores& stores,
    const boost::asio::any_io_executor& executor,
    pjsip_rx_data* rdata)
    : call_id_(std::move(call_id))
    , ctx_(ctx)
    , call_manager_(call_manager)
    , pool_(pjsip_endpt_create_pool(ctx->endpt_, call_id_.c_str(), kPoolInitial, kPoolIncrement))
    , media_bridge_(std::make_shared<MediaBridge>(executor))
    , caller_offer_sdp_(extract_sdp(rdata))
    , current_rdata_(rdata)
    , request_uri_(extract_request_uri(rdata))
    , caller_uri_(extract_from_uri(rdata))
    , caller_display_name_(extract_from_display_name(rdata))
    , setup_actions_(*this, stores)
    , dialog_actions_(*this)
    , setup_sm_(setup_actions_, call_id_)
    , dialog_sm_(dialog_actions_, call_id_) {
    // Must be installed before MediaBridge::start_bridge_loop() (see its own
    // thread-safety note) — the constructor body runs before any later action
    // can reach that call, so this satisfies it.
    media_bridge_->set_error_handler([call_id = call_id_](RelayLeg leg, RelayOp operation, std::error_code error) {
        Log::call()->error(
            "[{}] media relay error: {} {} failed: {}",
            call_id,
            to_string(leg),
            to_string(operation),
            error.message());
    });
}

CallSession::~CallSession() {
    release_mod_data(inv_caller());
    release_mod_data(inv_callee());
    exchange_.reset();
    Log::call()->trace("[{}] CallSession destroyed, releasing pool", call_id_);
    if (pool_ != nullptr) {
        pjsip_endpt_release_pool(ctx_->endpt_, pool_);
        pool_ = nullptr;
    }
}

bool CallSession::create_exchange(
    const std::string& destination,
    std::optional<Protocols::SupportedCodec> required_codec) {
    if (exchange_) {
        return false;
    }
    exchange_ = std::make_unique<OfferAnswerExchange>(*this, destination, required_codec);
    return true;
}

void CallSession::release_exchange() {
    exchange_.reset();
}

void CallSession::commit_offer_answer(std::string offer, std::string answer) {
    negotiated_offer_ = std::move(offer);
    negotiated_answer_ = std::move(answer);
}

void CallSession::report_call_started() {
    if (ctx_->call_events_ == nullptr) {
        return;
    }
    ctx_->call_events_->send_call_started(
        Protocols::CallStarted{
            .sip_call_id = call_id_,
            .caller = caller_uri_,
            .callee = request_uri_,
            .started_at = iso8601_now()});
}

void CallSession::report_call_answered() {
    answered_at_ = std::chrono::steady_clock::now();
    send_call_updated();
}

void CallSession::report_call_updated() {
    if (answered_at_) {
        send_call_updated();
    }
}

void CallSession::send_call_updated() {
    if (ctx_->call_events_ == nullptr) {
        return;
    }
    const auto& caller_codec = leg(Leg::kCaller).codec_;
    ctx_->call_events_->send_call_updated(
        Protocols::CallUpdated{
            .sip_call_id = call_id_,
            .route = outbound_destination_,
            .codec = caller_codec ? std::optional<std::string>{caller_codec->name_} : std::nullopt,
            .updated_at = iso8601_now()});
}

void CallSession::report_call_terminated(std::string_view status, std::optional<std::string> failure_reason) {
    if (terminated_reported_) {
        return;
    }
    terminated_reported_ = true;
    if (ctx_->call_events_ == nullptr) {
        return;
    }
    Protocols::CallTerminated event{
        .sip_call_id = call_id_,
        .status = std::string(status),
        .failure_reason = std::move(failure_reason),
        .ended_at = iso8601_now(),
        .duration_seconds = std::nullopt};
    if (status == Protocols::CallStatus::kSuccess && answered_at_) {
        event.duration_seconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - *answered_at_).count());
    }
    ctx_->call_events_->send_call_terminated(std::move(event));
}

std::optional<std::chrono::seconds> CallSession::established_duration() const {
    if (!answered_at_) {
        return std::nullopt;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - *answered_at_);
}

Leg CallSession::leg_for(const pjsip_inv_session* inv) const {
    return inv == leg(Leg::kCallee).inv_ ? Leg::kCallee : Leg::kCaller;
}

} // namespace SbcEngine
