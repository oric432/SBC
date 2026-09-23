#include "refer_handler.hpp"

#include <unordered_map>

#include <pjsip/sip_parser.h>

#include "core/utils/log.hpp"
#include "sip/call/call_session.hpp"
#include "sip/sm/events.hpp"

namespace SbcEngine {

namespace {
constexpr int kLastSipStatus = 699;

std::unordered_map<pjsip_evsub*, ReferHandler*>& subscriptions() {
    static std::unordered_map<pjsip_evsub*, ReferHandler*> registry;
    return registry;
}
} // namespace

ReferHandler::~ReferHandler() {
    stop_subscriptions();
}

void ReferHandler::stop_subscriptions() {
    if (outbound_sub_ != nullptr) {
        subscriptions().erase(outbound_sub_);
        pjsip_evsub_terminate(outbound_sub_, PJ_FALSE);
        outbound_sub_ = nullptr;
    }
    if (inbound_sub_ != nullptr) {
        subscriptions().erase(inbound_sub_);
        pjsip_evsub_terminate(inbound_sub_, PJ_FALSE);
        inbound_sub_ = nullptr;
    }
}

std::optional<std::string> ReferHandler::refer_to() const {
    if (pending_request_ == nullptr || pending_request_->msg_info.msg == nullptr) {
        return std::nullopt;
    }
    pjsip_msg* message = pending_request_->msg_info.msg;
    std::string header_name = "Refer-To";
    const pj_str_t refer_to_name = pj_str(header_name.data());
    auto* header = static_cast<pjsip_hdr*>(pjsip_msg_find_hdr_by_name(message, &refer_to_name, nullptr));
    if (header == nullptr || pjsip_msg_find_hdr_by_name(message, &refer_to_name, header->next) != nullptr) {
        return std::nullopt;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - PJSIP headers share a C struct prefix.
    const auto* refer_to_header = reinterpret_cast<const pjsip_generic_string_hdr*>(header);
    if (refer_to_header->hvalue.slen <= 0) {
        return std::nullopt;
    }
    std::string value{refer_to_header->hvalue.ptr, static_cast<std::size_t>(refer_to_header->hvalue.slen)};
    pjsip_uri* uri =
        pjsip_parse_uri(pending_request_->tp_info.pool, value.data(), value.size(), PJSIP_PARSE_URI_AS_NAMEADDR);
    if (uri == nullptr) {
        return std::nullopt;
    }
    uri = static_cast<pjsip_uri*>(pjsip_uri_get_uri(uri));
    if (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri)) {
        return std::nullopt;
    }
    return value;
}

void ReferHandler::reject(int status_code) {
    if (pending_request_ != nullptr) {
        pjsip_dlg_respond(
            pjsip_rdata_get_dlg(pending_request_),
            pending_request_,
            status_code,
            nullptr,
            nullptr,
            nullptr);
    }
}

void ReferHandler::busy() {
    reject(PJSIP_SC_REQUEST_PENDING);
}

void ReferHandler::start(Leg leg) {
    if (inbound_sub_ != nullptr || outbound_sub_ != nullptr) {
        reject(PJSIP_SC_REQUEST_PENDING);
        final_result_ = false;
        finish(false);
        return;
    }
    final_result_ = false;
    pending_result_.reset();
    const auto target = refer_to();
    if (!target) {
        reject(PJSIP_SC_BAD_REQUEST);
        finish(false);
        return;
    }
    if (pending_request_->msg_info.max_fwd != nullptr && pending_request_->msg_info.max_fwd->ivalue == 0) {
        reject(PJSIP_SC_TOO_MANY_HOPS);
        finish(false);
        return;
    }
    const auto& peer_leg = session_.leg(other(leg));
    pjsip_inv_session* peer = peer_leg.inv_;
    if (peer == nullptr || peer_leg.disconnected_) {
        reject(PJSIP_SC_SERVICE_UNAVAILABLE);
        finish(false);
        return;
    }

    pjsip_evsub_user callbacks{};
    callbacks.on_evsub_state = &ReferHandler::on_sub_state;
    callbacks.on_rx_notify = &ReferHandler::on_rx_notify;

    pj_status_t status = pjsip_xfer_create_uac(peer->dlg, &callbacks, &outbound_sub_);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] creating outbound REFER subscription failed ({})", session_.call_id(), status);
        reject(PJSIP_SC_SERVICE_UNAVAILABLE);
        finish(false);
        return;
    }
    subscriptions()[outbound_sub_] = this;

    status = pjsip_xfer_create_uas(pjsip_rdata_get_dlg(pending_request_), &callbacks, pending_request_, &inbound_sub_);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] creating inbound REFER subscription failed ({})", session_.call_id(), status);
        reject(PJSIP_SC_INTERNAL_SERVER_ERROR);
        finish(false);
        stop_subscriptions();
        return;
    }
    subscriptions()[inbound_sub_] = this;

    status = pjsip_xfer_accept(inbound_sub_, pending_request_, PJSIP_SC_ACCEPTED, nullptr);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] accepting REFER failed ({})", session_.call_id(), status);
        finish(false);
        stop_subscriptions();
        return;
    }
    if (!notify_inbound(PJSIP_EVSUB_STATE_ACTIVE, PJSIP_SC_TRYING)) {
        finish(false);
        stop_subscriptions();
        return;
    }

    pjsip_tx_data* outbound_request = nullptr;
    std::string target_value = *target;
    const pj_str_t uri = pj_str(target_value.data());
    status = pjsip_xfer_initiate(outbound_sub_, &uri, &outbound_request);
    if (status == PJ_SUCCESS) {
        auto* max_fwd =
            static_cast<pjsip_max_fwd_hdr*>(pjsip_msg_find_hdr(outbound_request->msg, PJSIP_H_MAX_FORWARDS, nullptr));
        const unsigned incoming_hops = pending_request_->msg_info.max_fwd != nullptr
                                           ? pending_request_->msg_info.max_fwd->ivalue
                                           : PJSIP_MAX_FORWARDS_VALUE;
        if (max_fwd != nullptr) {
            max_fwd->ivalue = incoming_hops - 1;
        }
        else {
            pjsip_msg_add_hdr(
                outbound_request->msg,
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - PJSIP header C API.
                reinterpret_cast<pjsip_hdr*>(pjsip_max_fwd_hdr_create(outbound_request->pool, incoming_hops - 1)));
        }
        status = pjsip_xfer_send_request(outbound_sub_, outbound_request);
    }
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] forwarding REFER failed ({})", session_.call_id(), status);
        notify_inbound(PJSIP_EVSUB_STATE_TERMINATED, PJSIP_SC_SERVICE_UNAVAILABLE);
        finish(false);
        stop_subscriptions();
    }
}

bool ReferHandler::notify_inbound(pjsip_evsub_state state, int status_code) {
    if (inbound_sub_ == nullptr) {
        return false;
    }
    pjsip_tx_data* notify = nullptr;
    pj_status_t status = pjsip_xfer_notify(inbound_sub_, state, status_code, nullptr, &notify);
    if (status == PJ_SUCCESS) {
        status = pjsip_xfer_send_request(inbound_sub_, notify);
    }
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] sending REFER NOTIFY failed ({})", session_.call_id(), status);
    }
    return status == PJ_SUCCESS;
}

void ReferHandler::finish(bool succeeded) {
    if (final_result_) {
        return;
    }
    final_result_ = true;
    if (session_.dialog_sm().is_processing()) {
        pending_result_ = succeeded;
        return;
    }
    if (succeeded) {
        session_.dialog_sm().process_event(ReferSucceeded{});
    }
    else {
        session_.dialog_sm().process_event(ReferFailed{});
    }
}

void ReferHandler::finish_dispatch() {
    if (!pending_result_) {
        return;
    }
    const bool succeeded = *pending_result_;
    pending_result_.reset();
    if (succeeded) {
        session_.dialog_sm().process_event(ReferSucceeded{});
    }
    else {
        session_.dialog_sm().process_event(ReferFailed{});
    }
}

void ReferHandler::on_sub_state(pjsip_evsub* sub, pjsip_event* event) {
    auto iter = subscriptions().find(sub);
    if (iter != subscriptions().end()) {
        iter->second->subscription_changed(sub, event);
    }
}

void ReferHandler::on_rx_notify(
    pjsip_evsub* sub,
    pjsip_rx_data* request,
    int* response_code,
    pj_str_t** response_text,
    pjsip_hdr* response_headers,
    pjsip_msg_body** response_body) {
    auto iter = subscriptions().find(sub);
    (void)response_text;
    (void)response_headers;
    (void)response_body;
    if (iter != subscriptions().end()) {
        iter->second->notification_received(request, response_code);
    }
    else {
        *response_code = PJSIP_SC_CALL_TSX_DOES_NOT_EXIST;
    }
}

void ReferHandler::subscription_changed(pjsip_evsub* sub, pjsip_event* event) {
    if (pjsip_evsub_get_state(sub) != PJSIP_EVSUB_STATE_TERMINATED) {
        return;
    }
    subscriptions().erase(sub);
    if (sub == inbound_sub_) {
        inbound_sub_ = nullptr;
        return;
    }
    outbound_sub_ = nullptr;
    if (final_result_) {
        return;
    }
    int status_code = PJSIP_SC_SERVICE_UNAVAILABLE;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) - PJSIP event C API.
    if (event != nullptr && event->type == PJSIP_EVENT_TSX_STATE && event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
        pjsip_rx_data* response = event->body.tsx_state.src.rdata;
        if (response != nullptr && response->msg_info.msg->type == PJSIP_RESPONSE_MSG &&
            response->msg_info.msg->line.status.code >= PJSIP_SC_MULTIPLE_CHOICES) {
            status_code = response->msg_info.msg->line.status.code;
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    notify_inbound(PJSIP_EVSUB_STATE_TERMINATED, status_code);
    finish(false);
}

void ReferHandler::notification_received(pjsip_rx_data* request, int* response_code) {
    const pjsip_msg_body* body = request->msg_info.msg->body;
    if (body == nullptr || body->data == nullptr || pj_stricmp2(&body->content_type.type, "message") != 0 ||
        pj_stricmp2(&body->content_type.subtype, "sipfrag") != 0) {
        *response_code = PJSIP_SC_BAD_REQUEST;
        return;
    }
    pjsip_status_line status_line{};
    const pj_status_t status = pjsip_parse_status_line(static_cast<char*>(body->data), body->len, &status_line);
    if (status != PJ_SUCCESS || status_line.code < PJSIP_SC_TRYING || status_line.code > kLastSipStatus) {
        *response_code = PJSIP_SC_BAD_REQUEST;
        return;
    }
    *response_code = PJSIP_SC_OK;
    if (final_result_) {
        return;
    }
    const bool is_final = status_line.code >= 200;
    if (inbound_sub_ != nullptr &&
        !notify_inbound(is_final ? PJSIP_EVSUB_STATE_TERMINATED : PJSIP_EVSUB_STATE_ACTIVE, status_line.code)) {
        finish(false);
        return;
    }
    if (is_final) {
        finish(status_line.code < PJSIP_SC_MULTIPLE_CHOICES);
    }
}

} // namespace SbcEngine
