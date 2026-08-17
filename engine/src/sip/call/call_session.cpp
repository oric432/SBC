#include "call_session.hpp"

#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
constexpr pj_size_t kPoolInitial = 4096;
constexpr pj_size_t kPoolIncrement = 4096;
} // namespace

CallSession::CallSession(std::string call_id, SbcContext* ctx, RoutesStore* routes_store, pjsip_rx_data* rdata)
    : call_id_(std::move(call_id))
    , ctx_(ctx)
    , pool_(pjsip_endpt_create_pool(ctx->endpt_, call_id_.c_str(), kPoolInitial, kPoolIncrement))
    , media_bridge_(std::make_shared<MediaBridge>(ctx->ioc_->get_executor()))
    , caller_offer_sdp_(extract_sdp(rdata))
    , current_rdata_(rdata)
    , request_uri_(extract_request_uri(rdata))
    , setup_actions_(*this, routes_store)
    , dialog_actions_(*this)
    , setup_sm_logger_("setup", call_id_)
    , dialog_sm_logger_("dialog", call_id_)
    , setup_sm_(setup_actions_, setup_sm_logger_)
    , dialog_sm_(dialog_actions_, dialog_sm_logger_) {}

CallSession::~CallSession() {
    Log::call()->trace("[{}] CallSession destroyed, releasing pool (mod_data[{}] freed)", call_id_, ctx_->module_id_);
    if (pool_ != nullptr) {
        pjsip_endpt_release_pool(ctx_->endpt_, pool_);
        pool_ = nullptr;
    }
}

} // namespace SbcEngine
