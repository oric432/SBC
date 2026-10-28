#include "extract_utils.hpp"

#include <array>
#include <cctype>
#include <format>
#include <string_view>

namespace SbcEngine {
namespace {

std::string_view trim_lws(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool ascii_iequals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto left = static_cast<unsigned char>(lhs[index]);
        const auto right = static_cast<unsigned char>(rhs[index]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

std::string raw_replaces_value(const pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg_buf == nullptr || rx_data->msg_info.len <= 0) {
        return {};
    }

    std::string_view message{rx_data->msg_info.msg_buf, static_cast<std::size_t>(rx_data->msg_info.len)};
    std::string value;
    bool found = false;
    bool first_line = true;
    while (!message.empty()) {
        const std::size_t newline = message.find('\n');
        std::string_view line = message.substr(0, newline);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        message = newline == std::string_view::npos ? std::string_view{} : message.substr(newline + 1);

        if (first_line) {
            first_line = false;
            continue;
        }
        if (line.empty()) {
            break;
        }
        if (found && (line.front() == ' ' || line.front() == '\t')) {
            value.push_back(' ');
            value.append(trim_lws(line));
            continue;
        }
        if (found) {
            break;
        }

        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos && ascii_iequals(trim_lws(line.substr(0, colon)), "Replaces")) {
            value.assign(trim_lws(line.substr(colon + 1)));
            found = true;
        }
    }
    return value;
}

} // namespace

std::string extract_method(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr) {
        return {};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C API
    const pj_str_t& name = rx_data->msg_info.msg->line.req.method.name;
    return {name.ptr, static_cast<std::size_t>(name.slen)};
}

bool has_replaces_header(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr) {
        return false;
    }
    static std::string replaces_name_string_s = "Replaces";
    const pj_str_t replaces_name = pj_str(replaces_name_string_s.data());
    return pjsip_msg_find_hdr_by_name(rx_data->msg_info.msg, &replaces_name, nullptr) != nullptr;
}

bool replaces_has_single_dialog_tags(pjsip_rx_data* rx_data) {
    const std::string value = raw_replaces_value(rx_data);
    if (value.empty()) {
        return false;
    }

    std::size_t to_tag_count = 0;
    std::size_t from_tag_count = 0;
    bool quoted = false;
    bool escaped = false;
    std::size_t parameter_start = std::string::npos;
    for (std::size_t index = 0; index <= value.size(); ++index) {
        const char character = index < value.size() ? value[index] : ';';
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && character == '\\') {
            escaped = true;
            continue;
        }
        if (character == '"') {
            quoted = !quoted;
            continue;
        }
        if (character != ';' || quoted) {
            continue;
        }
        if (parameter_start != std::string::npos) {
            const std::string_view parameter =
                trim_lws(std::string_view{value}.substr(parameter_start, index - parameter_start));
            const std::size_t equals = parameter.find('=');
            const std::string_view name = trim_lws(parameter.substr(0, equals));
            if (ascii_iequals(name, "to-tag")) {
                ++to_tag_count;
            }
            else if (ascii_iequals(name, "from-tag")) {
                ++from_tag_count;
            }
        }
        parameter_start = index + 1;
    }
    return to_tag_count == 1 && from_tag_count == 1;
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
