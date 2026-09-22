#include "extract_utils.hpp"

#include <array>
#include <format>

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

int extract_status_code(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr || rx_data->msg_info.msg->type != PJSIP_RESPONSE_MSG) {
        return 0;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C API
    return rx_data->msg_info.msg->line.status.code;
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

std::string extract_from_uri(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.from == nullptr || rx_data->msg_info.from->uri == nullptr) {
        return {};
    }
    std::array<char, PJSIP_MAX_URL_SIZE> buf{};
    const int len = pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, rx_data->msg_info.from->uri, buf.data(), buf.size());
    if (len < 0) {
        return {};
    }
    return {buf.data(), static_cast<std::size_t>(len)};
}

std::string extract_from_display_name(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.from == nullptr || rx_data->msg_info.from->uri == nullptr) {
        return {};
    }
    // From/To headers are always parsed as name-addr, see parse_hdr_fromto in sip_parser.c.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) — PJSIP C API
    const auto* name_addr = reinterpret_cast<const pjsip_name_addr*>(rx_data->msg_info.from->uri);
    if (name_addr->display.slen <= 0) {
        return {};
    }
    return {name_addr->display.ptr, static_cast<std::size_t>(name_addr->display.slen)};
}

std::string extract_uri_user(const std::string& uri) {
    auto scheme_end = uri.find(':');
    auto at_pos = uri.find('@');
    if (scheme_end == std::string::npos || at_pos == std::string::npos || at_pos <= scheme_end) {
        return {};
    }
    return uri.substr(scheme_end + 1, at_pos - scheme_end - 1);
}

std::string extract_uri_host(const std::string& uri) {
    // No "@" (e.g. a bare "sip:host:port" catch-all destination) -- the host
    // starts right after the scheme instead.
    auto at_pos = uri.find('@');
    std::size_t host_start{};
    if (at_pos == std::string::npos) {
        auto scheme_end = uri.find(':');
        if (scheme_end == std::string::npos) {
            return {};
        }
        host_start = scheme_end + 1;
    }
    else {
        host_start = at_pos + 1;
    }
    // A bracketed IPv6 literal (RFC 3261 19.1.3, e.g. "sip:alice@[2001:db8::1]:5060")
    // has colons inside the host itself, so the port-colon search below would stop
    // at the first one and truncate mid-address. PJSIP's own URI parser strips the
    // brackets from the host it stores (sip_parser.c), so this must match that --
    // both feed UsersStore::is_local_domain() and must agree on the same host string.
    if (host_start < uri.size() && uri[host_start] == '[') {
        auto close = uri.find(']', host_start + 1);
        if (close != std::string::npos) {
            return uri.substr(host_start + 1, close - host_start - 1);
        }
    }
    // The host ends at the port colon, or -- for "sip:alice@sbc.local;transport=udp"
    // style URIs -- at the first URI parameter (';') or header (`?`) instead;
    // stopping at ':' alone left those trailing on the "host" and broke
    // UsersStore::is_local_domain() lookups for exactly that URI shape.
    auto end = uri.find_first_of(":;?", host_start);
    if (end == std::string::npos) {
        end = uri.size();
    }
    return uri.substr(host_start, end - host_start);
}

std::string make_aor(const std::string& user, const std::string& host) {
    return std::format("{}@{}", user, host);
}

} // namespace SbcEngine
