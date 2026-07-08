#include "call_session.hpp"

namespace SbcEngine {

namespace {
constexpr pj_size_t kPoolInitial = 4096;
constexpr pj_size_t kPoolIncrement = 4096;
} // namespace

CallSession::CallSession(std::string call_id, SbcContext* ctx)
    : call_id_(std::move(call_id))
    , ctx_(ctx)
    , pool_(pjsip_endpt_create_pool(ctx->endpt_, call_id_.c_str(), kPoolInitial, kPoolIncrement))
    , rtp_caller_(call_id_ + "|caller", *ctx->ioc_)
    , rtp_callee_(call_id_ + "|callee", *ctx->ioc_)
    , setup_actions_(*this)
    , dialog_actions_(*this)
    , setup_sm_logger_("setup", call_id_)
    , dialog_sm_logger_("dialog", call_id_)
    , setup_sm_(setup_actions_, setup_sm_logger_)
    , dialog_sm_(dialog_actions_, dialog_sm_logger_) {}

CallSession::~CallSession() {
    if (pool_ != nullptr) {
        pjsip_endpt_release_pool(ctx_->endpt_, pool_);
        pool_ = nullptr;
    }
}

} // namespace SbcEngineEngine
