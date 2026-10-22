#pragma once

#include <optional>
#include <string>

#include <boost/asio/any_io_executor.hpp>
#include <pjsip.h>

#include "sip/call/pj_context.hpp"
#include "sip/engine_stores.hpp"

namespace SbcEngine {
class CallSession;
class CallManager;

struct ReplacementTarget {
    std::string replaced_session_id_;
    std::string call_id_;
    std::string to_tag_;
    std::string from_tag_;
    bool early_only_ = false;
    bool destination_is_uac_ = false;
};

// SIP request admission and responses that do not belong to a live call.
// Creates signaling legs, but leaves lifecycle activation to MessageRouter.
class SipRequestActions {
public:
    SipRequestActions(
        PjContext* ctx,
        CallManager* manager,
        const EngineStores& stores,
        boost::asio::any_io_executor executor)
        : ctx_(ctx)
        , call_manager_(manager)
        , stores_(stores)
        , executor_(std::move(executor)) {}

    CallSession* create_call(pjsip_rx_data* rx_data);
    [[nodiscard]] std::expected<std::optional<ReplacementTarget>, std::string> resolve_replacement(
        pjsip_rx_data* rx_data);
    void handle_unmatched_dialog_request(pjsip_rx_data* rx_data);
    void handle_unmatched_ack(pjsip_rx_data* rx_data);
    void reject_unsupported_method(pjsip_rx_data* rx_data);
    void reject_out_of_dialog_refer(pjsip_rx_data* rx_data);

private:
    friend struct SipRequestActionsTestAccess;

    [[nodiscard]] static std::optional<int> replacement_rejection_code(
        const pjsip_inv_session* invite,
        bool early_only);
    [[nodiscard]] static ReplacementTarget make_replacement_target(
        const pj_str_t& call_id,
        const pj_str_t& local_tag,
        const pj_str_t& remote_tag,
        pjsip_role_e role,
        bool early_only);
    void respond_stateless(pjsip_rx_data* rx_data, int code);

    PjContext* ctx_;
    CallManager* call_manager_;
    EngineStores stores_;
    boost::asio::any_io_executor executor_;
};
} // namespace SbcEngine
