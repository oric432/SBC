#include "extract_utils.hpp"

#include <array>

namespace SbcEngine {

std::string extract_method(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr) {
        return {};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C API
    const pj_str_t& name = rx_data->msg_info.msg->line.req.method.name;
    return {name.ptr, static_cast<std::size_t>(name.slen)};
}

std::string extract_sdp(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr) {
        return {};
    }
    const pjsip_msg_body* body = rx_data->msg_info.msg->body;
    if (body == nullptr || body->data == nullptr) {
        return {};
    }
    return {static_cast<const char*>(body->data), static_cast<std::size_t>(body->len)};
}

std::string extract_call_id(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.cid == nullptr) {
        return {};
    }
    const pj_str_t& cid = rx_data->msg_info.cid->id;
    return {cid.ptr, static_cast<std::size_t>(cid.slen)};
}

std::string extract_request_uri(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr) {
        return {};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C API
    const pjsip_uri* uri = rx_data->msg_info.msg->line.req.uri;
    if (uri == nullptr) {
        return {};
    }
    std::array<char, PJSIP_MAX_URL_SIZE> buf{};
    const int len = pjsip_uri_print(PJSIP_URI_IN_REQ_URI, uri, buf.data(), buf.size());
    if (len < 0) {
        return {};
    }
    return {buf.data(), static_cast<std::size_t>(len)};
}

std::string extract_uri_user(const std::string& uri) {
    auto scheme_end = uri.find(':');
    auto at_pos = uri.find('@');
    if (scheme_end == std::string::npos || at_pos == std::string::npos || at_pos <= scheme_end) {
        return {};
    }
    return uri.substr(scheme_end + 1, at_pos - scheme_end - 1);
}

} // namespace SbcEngine
