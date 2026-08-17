#include "call_session.hpp"

#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
constexpr pj_size_t kPoolInitial = 4096;
constexpr pj_size_t kPoolIncrement = 4096;
} // namespace

CallSession::CallSession(std::string call_id,
                         PjContext* ctx,
                         CallManager* call_manager,
                         RoutesStore* routes_store,
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
    , setup_actions_(*this, routes_store)
    , dialog_actions_(*this)
    , setup_sm_logger_("setup", call_id_)
    , dialog_sm_logger_("dialog", call_id_)
    , setup_sm_(setup_actions_, setup_sm_logger_)
    , dialog_sm_(dialog_actions_, dialog_sm_logger_) {
    // Must be installed before MediaBridge::start_bridge_loop() (see its own
    // thread-safety note) — the constructor body runs before any later action
    // can reach that call, so this satisfies it.
    media_bridge_->set_error_handler([call_id = call_id_](RelayLeg leg, RelayOp operation, std::error_code error) {
        Log::call()->error(
            "[{}] media relay error: {} {} failed: {}", call_id, to_string(leg), to_string(operation), error.message());
    });
}

CallSession::~CallSession() {
    Log::call()->trace("[{}] CallSession destroyed, releasing pool (mod_data[{}] freed)", call_id_, ctx_->module_id_);
    if (pool_ != nullptr) {
        pjsip_endpt_release_pool(ctx_->endpt_, pool_);
        pool_ = nullptr;
    }
}

} // namespace SbcEngine
