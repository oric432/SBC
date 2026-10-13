#include "refer_handler.hpp"

#include <array>
#include <optional>
#include <string_view>

#include <pjsip-ua/sip_replaces.h>
#include <pjsip/sip_parser.h>

#include "core/utils/log.hpp"
#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/sm/events.hpp"

namespace SbcEngine {

namespace {
constexpr int kLastSipStatus = 699;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
int g_module_id = -1;

int hex_value(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    return -1;
}

std::optional<std::string> percent_decode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%') {
            decoded.push_back(value[index]);
            continue;
        }
        if (index + 2 >= value.size()) {
            return std::nullopt;
        }
        const int high = hex_value(value[index + 1]);
        const int low = hex_value(value[index + 2]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        const char decoded_char = static_cast<char>((high << 4) | low);
        if (decoded_char == '\0') {
            return std::nullopt;
        }
        decoded.push_back(decoded_char);
        index += 2;
    }
    return decoded;
}
} // namespace

ReferHandler::~ReferHandler() {
    stop_subscriptions();
}

ReferHandler* ReferHandler::from_subscription(pjsip_evsub* sub) {
    if (sub == nullptr || g_module_id < 0) {
        return nullptr;
    }
    return static_cast<ReferHandler*>(pjsip_evsub_get_mod_data(sub, static_cast<unsigned>(g_module_id)));
}

void ReferHandler::attach(pjsip_evsub* sub) {
    if (sub != nullptr) {
        pjsip_evsub_set_mod_data(sub, static_cast<unsigned>(g_module_id), this);
    }
}

void ReferHandler::detach(pjsip_evsub* sub) const {
    if (sub != nullptr && g_module_id >= 0) {
        pjsip_evsub_set_mod_data(sub, static_cast<unsigned>(g_module_id), nullptr);
    }
}

void ReferHandler::stop_subscriptions() {
    if (outbound_sub_ != nullptr) {
        pjsip_evsub* subscription = outbound_sub_;
        outbound_sub_ = nullptr;
        detach(subscription);
        pjsip_evsub_terminate(subscription, PJ_FALSE);
    }
    if (inbound_sub_ != nullptr) {
        pjsip_evsub* subscription = inbound_sub_;
        inbound_sub_ = nullptr;
        detach(subscription);
        pjsip_evsub_terminate(subscription, PJ_FALSE);
    }
}

std::expected<std::string, int> ReferHandler::refer_to() const {
    if (pending_request_ == nullptr || pending_request_->msg_info.msg == nullptr) {
        return std::unexpected(PJSIP_SC_BAD_REQUEST);
    }
    pjsip_msg* message = pending_request_->msg_info.msg;
    std::string header_name = "Refer-To";
    const pj_str_t refer_to_name = pj_str(header_name.data());
    auto* header = static_cast<pjsip_hdr*>(pjsip_msg_find_hdr_by_name(message, &refer_to_name, nullptr));
    if (header == nullptr || pjsip_msg_find_hdr_by_name(message, &refer_to_name, header->next) != nullptr) {
        return std::unexpected(PJSIP_SC_BAD_REQUEST);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - PJSIP headers share a C struct prefix.
    const auto* refer_to_header = reinterpret_cast<const pjsip_generic_string_hdr*>(header);
    if (refer_to_header->hvalue.slen <= 0) {
        return std::unexpected(PJSIP_SC_BAD_REQUEST);
    }
    std::string value{refer_to_header->hvalue.ptr, static_cast<std::size_t>(refer_to_header->hvalue.slen)};
    std::string parse_buffer = value;
    pjsip_uri* uri = pjsip_parse_uri(
        pending_request_->tp_info.pool,
        parse_buffer.data(),
        parse_buffer.size(),
        PJSIP_PARSE_URI_AS_NAMEADDR);
    if (uri == nullptr) {
        return std::unexpected(PJSIP_SC_BAD_REQUEST);
    }
    auto* sip_uri = static_cast<pjsip_sip_uri*>(pjsip_uri_get_uri(uri));
    if (!PJSIP_URI_SCHEME_IS_SIP(sip_uri) && !PJSIP_URI_SCHEME_IS_SIPS(sip_uri)) {
        return std::unexpected(PJSIP_SC_BAD_REQUEST);
    }

    static std::string replaces_name_string = "Replaces";
    const pj_str_t replaces_name = pj_str(replaces_name_string.data());
    pjsip_param* replaces_param = pjsip_param_find(&sip_uri->header_param, &replaces_name);
    if (replaces_param == nullptr) {
        return value;
    }
    if (replaces_param->value.slen <= 0) {
        return std::unexpected(PJSIP_SC_BAD_REQUEST);
    }
    for (pjsip_param* param = replaces_param->next; param != &sip_uri->header_param; param = param->next) {
        if (pj_stricmp(&param->name, &replaces_name) == 0) {
            return std::unexpected(PJSIP_SC_BAD_REQUEST);
        }
    }

    auto replaces_value = percent_decode(
        std::string_view{replaces_param->value.ptr, static_cast<std::size_t>(replaces_param->value.slen)});
    if (!replaces_value) {
        return std::unexpected(PJSIP_SC_BAD_REQUEST);
    }
    auto* replaces = static_cast<pjsip_replaces_hdr*>(pjsip_parse_hdr(
        pending_request_->tp_info.pool,
        &replaces_name,
        replaces_value->data(),
        replaces_value->size(),
        nullptr));
    if (replaces == nullptr || replaces->call_id.slen <= 0 || replaces->to_tag.slen <= 0 ||
        replaces->from_tag.slen <= 0) {
        return std::unexpected(PJSIP_SC_BAD_REQUEST);
    }

    pjsip_dialog* referenced_dialog =
        pjsip_ua_find_dialog(&replaces->call_id, &replaces->to_tag, &replaces->from_tag, PJ_TRUE);
    if (referenced_dialog == nullptr) {
        return std::unexpected(PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
    }
    CallSession* referenced_session = session_.call_manager()->find_by_dialog(referenced_dialog);
    const Leg referenced_leg =
        referenced_session != nullptr && referenced_session->leg(Leg::kCallee).dialog_ == referenced_dialog
            ? Leg::kCallee
            : Leg::kCaller;
    pjsip_dlg_dec_lock(referenced_dialog);
    if (referenced_session == nullptr) {
        return std::unexpected(PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
    }

    const auto& translated_leg = referenced_session->leg(other(referenced_leg));
    pjsip_dialog* translated_dialog = translated_leg.dialog_;
    if (translated_dialog == nullptr || translated_leg.disconnected_) {
        return std::unexpected(PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
    }
    pjsip_dlg_inc_lock(translated_dialog);
    replaces->call_id = translated_dialog->call_id->id;
    replaces->to_tag = translated_dialog->remote.info->tag;
    replaces->from_tag = translated_dialog->local.info->tag;
    std::array<char, PJSIP_MAX_URL_SIZE> replaces_buffer{};
    const int replaces_length = pjsip_hdr_print_on(replaces, replaces_buffer.data(), replaces_buffer.size());
    pjsip_dlg_dec_lock(translated_dialog);
    constexpr std::string_view kReplacesPrefix = "Replaces: ";
    if (replaces_length <= 0) {
        return std::unexpected(PJSIP_SC_INTERNAL_SERVER_ERROR);
    }
    const std::string_view printed_replaces{replaces_buffer.data(), static_cast<std::size_t>(replaces_length)};
    if (!printed_replaces.starts_with(kReplacesPrefix)) {
        return std::unexpected(PJSIP_SC_INTERNAL_SERVER_ERROR);
    }
    std::string translated_replaces{
        replaces_buffer.data() + kReplacesPrefix.size(),
        static_cast<std::size_t>(replaces_length) - kReplacesPrefix.size()};
    replaces_param->value = pj_str(translated_replaces.data());

    std::array<char, PJSIP_MAX_URL_SIZE> target_buffer{};
    const int target_length = pjsip_uri_print(PJSIP_URI_IN_OTHER, uri, target_buffer.data(), target_buffer.size());
    if (target_length < 0) {
        return std::unexpected(PJSIP_SC_INTERNAL_SERVER_ERROR);
    }
    return std::string{target_buffer.data(), static_cast<std::size_t>(target_length)};
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

bool ReferHandler::start(Leg leg) {
    if (inbound_sub_ != nullptr || outbound_sub_ != nullptr) {
        reject(PJSIP_SC_REQUEST_PENDING);
        result_delivered_ = false;
        return false;
    }
    result_delivered_ = false;
    if (g_module_id == -1) {
        g_module_id = session_.ctx()->module_id_;
    }
    if (g_module_id == -1) {
        reject(PJSIP_SC_INTERNAL_SERVER_ERROR);
        return false;
    }

    const auto target = refer_to();
    if (!target) {
        reject(target.error());
        return false;
    }
    pjsip_msg* incoming_message = pending_request_->msg_info.msg;
    static std::string referred_by_name_string = "Referred-By";
    const pj_str_t referred_by_name = pj_str(referred_by_name_string.data());
    auto* referred_by =
        static_cast<pjsip_hdr*>(pjsip_msg_find_hdr_by_name(incoming_message, &referred_by_name, nullptr));
    if (referred_by != nullptr &&
        pjsip_msg_find_hdr_by_name(incoming_message, &referred_by_name, referred_by->next) != nullptr) {
        reject(PJSIP_SC_BAD_REQUEST);
        return false;
    }
    if (pending_request_->msg_info.max_fwd != nullptr && pending_request_->msg_info.max_fwd->ivalue == 0) {
        reject(PJSIP_SC_TOO_MANY_HOPS);
        return false;
    }
    const auto& peer_leg = session_.leg(other(leg));
    pjsip_inv_session* peer = peer_leg.inv_;
    if (peer == nullptr || peer_leg.disconnected_) {
        reject(PJSIP_SC_SERVICE_UNAVAILABLE);
        return false;
    }

    pjsip_evsub_user callbacks{};
    callbacks.on_evsub_state = &ReferHandler::on_sub_state;
    callbacks.on_rx_notify = &ReferHandler::on_rx_notify;

    pj_status_t status = pjsip_xfer_create_uac(peer->dlg, &callbacks, &outbound_sub_);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] creating outbound REFER subscription failed ({})", session_.call_id(), status);
        reject(PJSIP_SC_SERVICE_UNAVAILABLE);
        return false;
    }
    attach(outbound_sub_);

    status = pjsip_xfer_create_uas(pjsip_rdata_get_dlg(pending_request_), &callbacks, pending_request_, &inbound_sub_);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] creating inbound REFER subscription failed ({})", session_.call_id(), status);
        reject(PJSIP_SC_INTERNAL_SERVER_ERROR);
        stop_subscriptions();
        return false;
    }
    attach(inbound_sub_);

    status = pjsip_xfer_accept(inbound_sub_, pending_request_, PJSIP_SC_ACCEPTED, nullptr);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] accepting REFER failed ({})", session_.call_id(), status);
        stop_subscriptions();
        return false;
    }
    if (!notify_inbound(PJSIP_EVSUB_STATE_ACTIVE, PJSIP_SC_TRYING)) {
        fail_after_accept(PJSIP_SC_SERVICE_UNAVAILABLE);
        return false;
    }

    pjsip_tx_data* outbound_request = nullptr;
    std::string target_value = *target;
    const pj_str_t uri = pj_str(target_value.data());
    status = pjsip_xfer_initiate(outbound_sub_, &uri, &outbound_request);
    if (status == PJ_SUCCESS) {
        if (referred_by != nullptr) {
            pjsip_msg_add_hdr(
                outbound_request->msg,
                static_cast<pjsip_hdr*>(pjsip_hdr_clone(outbound_request->pool, referred_by)));
        }
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
        fail_after_accept(PJSIP_SC_SERVICE_UNAVAILABLE);
        return false;
    }
    return true;
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

void ReferHandler::fail_after_accept(int status_code) {
    notify_inbound(PJSIP_EVSUB_STATE_TERMINATED, status_code);
    stop_subscriptions();
}

void ReferHandler::finish(ReferOutcome outcome) {
    if (result_delivered_) {
        return;
    }
    result_delivered_ = true;
    session_.dialog_sm().process_event(ReferCompleted{outcome});
}

void ReferHandler::on_sub_state(pjsip_evsub* sub, pjsip_event* event) {
    if (ReferHandler* handler = from_subscription(sub); handler != nullptr) {
        handler->subscription_changed(sub, event);
    }
}

void ReferHandler::on_rx_notify(
    pjsip_evsub* sub,
    pjsip_rx_data* request,
    int* response_code,
    pj_str_t** response_text,
    pjsip_hdr* response_headers,
    pjsip_msg_body** response_body) {
    (void)response_text;
    (void)response_headers;
    (void)response_body;
    if (ReferHandler* handler = from_subscription(sub); handler != nullptr) {
        handler->notification_received(request, response_code);
    }
    else {
        *response_code = PJSIP_SC_CALL_TSX_DOES_NOT_EXIST;
    }
}

void ReferHandler::subscription_changed(pjsip_evsub* sub, pjsip_event* event) {
    if (pjsip_evsub_get_state(sub) != PJSIP_EVSUB_STATE_TERMINATED) {
        return;
    }
    detach(sub);
    if (sub == inbound_sub_) {
        inbound_sub_ = nullptr;
        return;
    }
    outbound_sub_ = nullptr;
    if (result_delivered_) {
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
    fail_after_accept(status_code);
    finish(ReferOutcome::kFailed);
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
    if (result_delivered_) {
        return;
    }
    const bool is_final = status_line.code >= 200;
    if (is_final && status_line.code >= PJSIP_SC_MULTIPLE_CHOICES) {
        fail_after_accept(status_line.code);
        finish(ReferOutcome::kFailed);
        return;
    }
    if (inbound_sub_ != nullptr &&
        !notify_inbound(is_final ? PJSIP_EVSUB_STATE_TERMINATED : PJSIP_EVSUB_STATE_ACTIVE, status_line.code)) {
        fail_after_accept(PJSIP_SC_SERVICE_UNAVAILABLE);
        finish(ReferOutcome::kFailed);
        return;
    }
    if (is_final) {
        finish(ReferOutcome::kSucceeded);
    }
}

} // namespace SbcEngine
