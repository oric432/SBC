#include "registrar_actions.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "net/rtp/rtp_inactivity_timer.hpp"
#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {

std::string to_std_string(const pj_str_t& str) {
    return {str.ptr, static_cast<std::size_t>(str.slen)};
}

// PJSIP's lookup2 callback has no user-data slot, so -- like g_active_stack
// in pjsip_init.cpp -- the one UsersStore this process ever has lives here.
// Set once, by the one RegistrarActions MessageRouter ever constructs.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) -- see above
UsersStore* g_users_store = nullptr;

pj_status_t lookup_credential(pj_pool_t* pool, const pjsip_auth_lookup_cred_param* param, pjsip_cred_info* cred_info) {
    const std::string username = to_std_string(param->acc_name);
    const std::string realm = to_std_string(param->realm);

    const auto user = g_users_store->find(username, realm);
    if (!user) {
        return PJSIP_EAUTHACCNOTFOUND;
    }

    pj_bzero(cred_info, sizeof(*cred_info));
    pj_strdup2(pool, &cred_info->realm, realm.c_str());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — PJSIP C API (pj_str never mutates the buffer)
    cred_info->scheme = pj_str(const_cast<char*>("Digest"));
    pj_strdup2(pool, &cred_info->username, username.c_str());
    cred_info->data_type = PJSIP_CRED_DATA_DIGEST;
    pj_strdup2(pool, &cred_info->data, user->ha1.c_str());
    cred_info->algorithm_type = PJSIP_AUTH_ALGORITHM_MD5;
    return PJ_SUCCESS;
}

pjsip_uri* parse_uri(pj_pool_t* pool, const std::string& uri_str) {
    // pjsip_parse_uri may modify the buffer in place, so it needs its own
    // pool-allocated, mutable copy rather than uri_str's own storage.
    auto* buf = static_cast<char*>(pj_pool_alloc(pool, uri_str.size() + 1));
    std::memcpy(buf, uri_str.data(), uri_str.size());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) — PJSIP C API
    buf[uri_str.size()] = '\0';
    return pjsip_parse_uri(pool, buf, uri_str.size(), 0);
}

std::string print_contact_uri(pj_pool_t* /*pool*/, pjsip_uri* uri) {
    std::array<char, PJSIP_MAX_URL_SIZE> buf{};
    const int len = pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR, uri, buf.data(), buf.size());
    if (len < 0) {
        return {};
    }
    return {buf.data(), static_cast<std::size_t>(len)};
}

int raw_expires_for_contact(
    const pjsip_contact_hdr* contact,
    std::optional<int> top_level_expires,
    int default_expires) {
    if (contact->expires != PJSIP_EXPIRES_NOT_SPECIFIED) {
        return static_cast<int>(contact->expires);
    }
    if (top_level_expires) {
        return *top_level_expires;
    }
    return default_expires;
}

// PJSIP has no typed header for User-Agent (sip_msg.h lists it as
// PJSIP_H_USER_AGENT_UNIMP, "use pjsip_generic_string_hdr" instead), so it
// has to be looked up by name.
std::optional<std::string> extract_user_agent(const pjsip_msg* msg) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — PJSIP C API (pj_str never mutates the buffer)
    const pj_str_t name = pj_str(const_cast<char*>("User-Agent"));
    const auto* hdr = static_cast<const pjsip_generic_string_hdr*>(pjsip_msg_find_hdr_by_name(msg, &name, nullptr));
    if (hdr == nullptr) {
        return std::nullopt;
    }
    return to_std_string(hdr->hvalue);
}

} // namespace

RegistrarActions::RegistrarActions(
    PjContext* ctx,
    UsersStore* users_store,
    BindingStore* binding_store,
    RegistrarConfig* config)
    : ctx_(ctx)
    , users_store_(users_store)
    , binding_store_(binding_store)
    , config_(config) {
    g_users_store = users_store;
}

void RegistrarActions::handle(pjsip_rx_data* rdata) {
    const auto realm = local_domain(rdata);
    if (!realm) {
        respond(rdata, PJSIP_SC_NOT_FOUND);
        return;
    }

    int status_code = PJSIP_SC_OK;
    const pj_status_t status = verify(rdata, *realm, &status_code);
    if (status == PJ_SUCCESS) {
        const auto* to_uri = static_cast<pjsip_sip_uri*>(pjsip_uri_get_uri(rdata->msg_info.to->uri));
        const std::string to_user = to_std_string(to_uri->user);
        // A valid digest response only proves the requester knows some
        // account's credentials in this realm -- without this check, that
        // account could register any other local user's AOR to its own
        // source address and hijack that user's inbound calls.
        const auto authenticated_user = authenticated_username(rdata);
        if (!authenticated_user || *authenticated_user != to_user) {
            respond(rdata, PJSIP_SC_FORBIDDEN);
            return;
        }
        process_registration(rdata, *realm, to_user);
        return;
    }
    if (status == PJSIP_EAUTHNOAUTH) {
        send_challenge(rdata, *realm);
        return;
    }
    // pjsip_auth_srv_verify() always sets status_code to a 4xx for every
    // other failure it can return, but the algorithm-unsupported path
    // (PJSIP_EINVALIDALGORITHM) leaves it at the 200 it initializes to --
    // never let that fall through as a success-shaped status code.
    respond(rdata, status_code >= PJSIP_SC_BAD_REQUEST ? status_code : PJSIP_SC_FORBIDDEN);
}

std::optional<std::string> RegistrarActions::local_domain(pjsip_rx_data* rdata) const {
    const auto* to_uri = static_cast<pjsip_uri*>(pjsip_uri_get_uri(rdata->msg_info.to->uri));
    if (!PJSIP_URI_SCHEME_IS_SIP(to_uri)) {
        return std::nullopt;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) — PJSIP C API
    std::string host = to_std_string(reinterpret_cast<const pjsip_sip_uri*>(to_uri)->host);
    if (!users_store_->is_local_domain(host)) {
        return std::nullopt;
    }
    return host;
}

std::optional<std::string> RegistrarActions::authenticated_username(pjsip_rx_data* rdata) {
    const auto* auth_hdr =
        static_cast<pjsip_authorization_hdr*>(pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_AUTHORIZATION, nullptr));
    if (auth_hdr == nullptr) {
        return std::nullopt;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C API
    return to_std_string(auth_hdr->credential.digest.username);
}

pj_status_t RegistrarActions::verify(pjsip_rx_data* rdata, const std::string& realm, int* status_code) {
    pjsip_auth_srv auth_srv;
    pjsip_auth_srv_init_param init_param;
    pj_bzero(&init_param, sizeof(init_param));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — PJSIP C API (pj_str never mutates the buffer)
    const pj_str_t realm_str = pj_str(const_cast<char*>(realm.c_str()));
    init_param.realm = &realm_str;
    init_param.lookup2 = &lookup_credential;
    init_param.options = 0;

    if (pjsip_auth_srv_init2(rdata->tp_info.pool, &auth_srv, &init_param) != PJ_SUCCESS) {
        *status_code = PJSIP_SC_INTERNAL_SERVER_ERROR;
        return PJSIP_EAUTHACCNOTFOUND;
    }

    *status_code = PJSIP_SC_OK;
    return pjsip_auth_srv_verify(&auth_srv, rdata, status_code);
}

void RegistrarActions::send_challenge(pjsip_rx_data* rdata, const std::string& realm) {
    if (ctx_ == nullptr || ctx_->endpt_ == nullptr) {
        Log::sip()->error("send_challenge: no endpoint");
        return;
    }

    pjsip_tx_data* tdata = nullptr;
    const pj_status_t create_status =
        pjsip_endpt_create_response(ctx_->endpt_, rdata, PJSIP_SC_UNAUTHORIZED, nullptr, &tdata);
    if (create_status != PJ_SUCCESS) {
        Log::sip()->error("send_challenge: pjsip_endpt_create_response failed ({})", create_status);
        return;
    }

    pjsip_auth_srv auth_srv;
    pjsip_auth_srv_init_param init_param;
    pj_bzero(&init_param, sizeof(init_param));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — PJSIP C API (pj_str never mutates the buffer)
    const pj_str_t realm_str = pj_str(const_cast<char*>(realm.c_str()));
    init_param.realm = &realm_str;
    init_param.lookup2 = &lookup_credential;
    init_param.options = 0;
    // No qop: unqualified digest is valid per RFC 2617 and every target
    // client here (Cisco desk phones, SIPp) supports it -- one less moving
    // part than negotiating qop=auth.
    const pj_status_t init_status = pjsip_auth_srv_init2(tdata->pool, &auth_srv, &init_param);
    if (init_status != PJ_SUCCESS) {
        Log::sip()->error("send_challenge: pjsip_auth_srv_init2 failed ({})", init_status);
        pjsip_tx_data_dec_ref(tdata);
        return;
    }
    pjsip_auth_srv_challenge(&auth_srv, nullptr, nullptr, nullptr, PJ_FALSE, tdata);

    pjsip_response_addr res_addr;
    const pj_status_t addr_status = pjsip_get_response_addr(tdata->pool, rdata, &res_addr);
    if (addr_status != PJ_SUCCESS) {
        Log::sip()->error("send_challenge: pjsip_get_response_addr failed ({})", addr_status);
        pjsip_tx_data_dec_ref(tdata);
        return;
    }
    pjsip_endpt_send_response(ctx_->endpt_, &res_addr, tdata, nullptr, nullptr);
}

void RegistrarActions::process_registration(
    pjsip_rx_data* rdata,
    const std::string& realm,
    const std::string& to_user) {
    const pjsip_msg* msg = rdata->msg_info.msg;
    const std::string aor = make_aor(to_user, realm);
    const auto user_agent = extract_user_agent(msg);

    const auto* expires_hdr = static_cast<pjsip_expires_hdr*>(pjsip_msg_find_hdr(msg, PJSIP_H_EXPIRES, nullptr));
    const std::optional<int> top_level_expires =
        expires_hdr != nullptr ? std::optional<int>(static_cast<int>(expires_hdr->ivalue)) : std::nullopt;

    std::vector<pjsip_contact_hdr*> contacts;
    for (auto* hdr = static_cast<pjsip_contact_hdr*>(pjsip_msg_find_hdr(msg, PJSIP_H_CONTACT, nullptr)); hdr != nullptr;
         hdr = static_cast<pjsip_contact_hdr*>(pjsip_msg_find_hdr(msg, PJSIP_H_CONTACT, hdr->next))) {
        contacts.push_back(hdr);
    }

    if (contacts.empty()) {
        // Query: RFC 3261 10.3 -- REGISTER with no Contact just reports what's on file.
        send_ok(rdata, aor);
        return;
    }

    const bool has_star = std::ranges::any_of(contacts, [](const auto* contact) { return contact->star != 0; });
    if (has_star) {
        if (contacts.size() > 1) {
            respond(rdata, PJSIP_SC_BAD_REQUEST);
            return;
        }
        const int raw = raw_expires_for_contact(contacts.front(), top_level_expires, config_->max_expires_s_);
        if (raw != 0) {
            respond(rdata, PJSIP_SC_BAD_REQUEST);
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (const auto& binding : binding_store_->find_live(aor, now)) {
            mirror_registration(aor, binding, /*removed=*/true, user_agent);
        }
        binding_store_->remove_all(aor);
        send_ok(rdata, aor);
        return;
    }

    // Reject a too-brief request before touching BindingStore at all.
    if (reject_if_too_brief(rdata, contacts, top_level_expires)) {
        return;
    }

    const std::string call_id = to_std_string(rdata->msg_info.cid->id);
    const int cseq = rdata->msg_info.cseq->cseq;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) — PJSIP C API
    const std::string source_address(rdata->pkt_info.src_name);
    const int source_port = rdata->pkt_info.src_port;
    const auto now = std::chrono::steady_clock::now();

    std::vector<Binding> new_bindings;
    new_bindings.reserve(contacts.size());
    for (const auto* contact : contacts) {
        std::string contact_uri = print_contact_uri(rdata->tp_info.pool, contact->uri);
        // print_contact_uri() returns "" on a pjsip_uri_print() failure --
        // storing that would produce a Contact header with no URI in a later
        // 200 OK (parse_uri() returns nullptr for an empty string).
        if (contact_uri.empty()) {
            respond(rdata, PJSIP_SC_BAD_REQUEST);
            return;
        }
        const int raw = raw_expires_for_contact(contact, top_level_expires, config_->max_expires_s_);
        const int granted = raw == 0 ? 0 : std::min(raw, config_->max_expires_s_);
        new_bindings.push_back(
            Binding{
                .contact_uri_ = std::move(contact_uri),
                .source_address_ = source_address,
                .source_port_ = source_port,
                .transport_ = "udp",
                .call_id_ = call_id,
                .cseq_ = cseq,
                .expires_at_ = now + std::chrono::seconds(granted),
                .refreshed_at_ = now,
                .mirrored_at_ = {}});
    }

    const std::vector<bool> mirror_decisions = decide_mirrors(aor, new_bindings, now);

    if (binding_store_->apply_contacts(aor, new_bindings) == BindingStore::ApplyResult::kCallIdCseqConflict) {
        respond(rdata, PJSIP_SC_BAD_REQUEST);
        return;
    }

    for (std::size_t i = 0; i < new_bindings.size(); ++i) {
        if (!mirror_decisions[i]) {
            continue;
        }
        const auto& binding = new_bindings[i];
        const bool removed = binding.expires_at_ <= binding.refreshed_at_;
        mirror_registration(aor, binding, removed, user_agent);
    }

    send_ok(rdata, aor);
}

std::vector<bool> RegistrarActions::decide_mirrors(
    const std::string& aor,
    std::vector<Binding>& new_bindings,
    std::chrono::steady_clock::time_point now) const {
    // Snapshot what was live before the caller's apply_contacts() call
    // overwrites it -- each decision below needs to compare an incoming
    // contact against its own prior state.
    std::unordered_map<std::string, Binding> previous_by_contact;
    for (auto& binding : binding_store_->find_live(aor, now)) {
        previous_by_contact.emplace(binding.contact_uri_, std::move(binding));
    }

    std::vector<bool> decisions;
    decisions.reserve(new_bindings.size());
    for (auto& binding : new_bindings) {
        const bool removed = binding.expires_at_ <= binding.refreshed_at_;
        const auto previous_iter = previous_by_contact.find(binding.contact_uri_);
        const std::optional<Binding> previous =
            previous_iter != previous_by_contact.end() ? std::optional<Binding>(previous_iter->second) : std::nullopt;
        const bool mirror = should_mirror(previous, binding, removed, now);
        // Skipped: carry the prior mirror timestamp forward rather than
        // losing it -- should_mirror() guarantees previous is set whenever
        // mirror is false (only removed/new bindings mirror unconditionally).
        binding.mirrored_at_ = mirror ? now : previous->mirrored_at_;
        decisions.push_back(mirror);
    }
    return decisions;
}

bool RegistrarActions::should_mirror(
    const std::optional<Binding>& previous,
    const Binding& incoming,
    bool removed,
    std::chrono::steady_clock::time_point now) {
    if (removed || !previous) {
        return true;
    }
    if (previous->source_address_ != incoming.source_address_ || previous->source_port_ != incoming.source_port_ ||
        previous->transport_ != incoming.transport_) {
        return true;
    }
    const auto granted_lifetime = incoming.expires_at_ - incoming.refreshed_at_;
    return now - previous->mirrored_at_ >= granted_lifetime / 2;
}

bool RegistrarActions::reject_if_too_brief(
    pjsip_rx_data* rdata,
    const std::vector<pjsip_contact_hdr*>& contacts,
    std::optional<int> top_level_expires) {
    for (const auto* contact : contacts) {
        const int raw = raw_expires_for_contact(contact, top_level_expires, config_->max_expires_s_);
        if (raw == 0 || raw >= config_->min_expires_s_) {
            continue;
        }
        pjsip_tx_data* tdata = nullptr;
        const pj_status_t create_status =
            pjsip_endpt_create_response(ctx_->endpt_, rdata, PJSIP_SC_INTERVAL_TOO_BRIEF, nullptr, &tdata);
        if (create_status != PJ_SUCCESS) {
            Log::sip()->error("reject_if_too_brief: pjsip_endpt_create_response failed ({})", create_status);
            return true;
        }
        auto* min_expires = pjsip_min_expires_hdr_create(tdata->pool, static_cast<unsigned>(config_->min_expires_s_));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) — PJSIP C API
        pjsip_msg_add_hdr(tdata->msg, reinterpret_cast<pjsip_hdr*>(min_expires));
        pjsip_response_addr res_addr;
        const pj_status_t addr_status = pjsip_get_response_addr(tdata->pool, rdata, &res_addr);
        if (addr_status != PJ_SUCCESS) {
            Log::sip()->error("reject_if_too_brief: pjsip_get_response_addr failed ({})", addr_status);
            pjsip_tx_data_dec_ref(tdata);
            return true;
        }
        pjsip_endpt_send_response(ctx_->endpt_, &res_addr, tdata, nullptr, nullptr);
        return true;
    }
    return false;
}

void RegistrarActions::respond(pjsip_rx_data* rdata, int status_code) {
    if (ctx_ == nullptr || ctx_->endpt_ == nullptr) {
        Log::sip()->error("respond: no endpoint");
        return;
    }
    pjsip_tx_data* tdata = nullptr;
    const pj_status_t create_status = pjsip_endpt_create_response(ctx_->endpt_, rdata, status_code, nullptr, &tdata);
    if (create_status != PJ_SUCCESS) {
        Log::sip()->error("respond: pjsip_endpt_create_response failed ({})", create_status);
        return;
    }
    pjsip_response_addr res_addr;
    const pj_status_t addr_status = pjsip_get_response_addr(tdata->pool, rdata, &res_addr);
    if (addr_status != PJ_SUCCESS) {
        Log::sip()->error("respond: pjsip_get_response_addr failed ({})", addr_status);
        pjsip_tx_data_dec_ref(tdata);
        return;
    }
    pjsip_endpt_send_response(ctx_->endpt_, &res_addr, tdata, nullptr, nullptr);
}

void RegistrarActions::send_ok(pjsip_rx_data* rdata, const std::string& aor) {
    if (ctx_ == nullptr || ctx_->endpt_ == nullptr) {
        Log::sip()->error("send_ok: no endpoint");
        return;
    }
    pjsip_tx_data* tdata = nullptr;
    const pj_status_t create_status = pjsip_endpt_create_response(ctx_->endpt_, rdata, PJSIP_SC_OK, nullptr, &tdata);
    if (create_status != PJ_SUCCESS) {
        Log::sip()->error("send_ok: pjsip_endpt_create_response failed ({})", create_status);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    for (const auto& binding : binding_store_->find_live(aor, now)) {
        auto* uri = parse_uri(tdata->pool, binding.contact_uri_);
        // Mirrors process_registration()'s write-time guard against
        // parse_uri() returning nullptr -- the stored contact_uri_ already
        // round-tripped through pjsip_uri_print() once, but re-parsing it
        // here is a second, independent chance to fail.
        if (uri == nullptr) {
            Log::sip()->error("send_ok: failed to re-parse stored contact URI, skipping ({})", binding.contact_uri_);
            continue;
        }
        auto* contact = pjsip_contact_hdr_create(tdata->pool);
        contact->uri = uri;
        contact->expires = static_cast<pj_uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(binding.expires_at_ - now).count());
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) — PJSIP C API
        pjsip_msg_add_hdr(tdata->msg, reinterpret_cast<pjsip_hdr*>(contact));
    }

    pjsip_response_addr res_addr;
    const pj_status_t addr_status = pjsip_get_response_addr(tdata->pool, rdata, &res_addr);
    if (addr_status != PJ_SUCCESS) {
        Log::sip()->error("send_ok: pjsip_get_response_addr failed ({})", addr_status);
        pjsip_tx_data_dec_ref(tdata);
        return;
    }
    pjsip_endpt_send_response(ctx_->endpt_, &res_addr, tdata, nullptr, nullptr);
}

void RegistrarActions::mirror_registration(
    const std::string& aor,
    const Binding& binding,
    bool removed,
    const std::optional<std::string>& user_agent) {
    if (sink_ == nullptr) {
        return;
    }
    const auto expires_in_s = removed ? 0
                                      : static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                                                             binding.expires_at_ - std::chrono::steady_clock::now())
                                                             .count());
    sink_->send_registration(
        Protocols::RegistrationEvent{
            .aor = aor,
            .contact_uri = binding.contact_uri_,
            .source_address = binding.source_address_,
            .source_port = binding.source_port_,
            .transport = binding.transport_,
            .user_agent = user_agent,
            .expires_in_s = expires_in_s,
            .removed = removed});
}

void RegistrarActions::start_binding_sweep_timer(
    const boost::asio::any_io_executor& executor,
    std::chrono::steady_clock::duration interval) {
    binding_sweep_timer_ = std::make_shared<RtpInactivityTimer>(executor, interval);
    binding_sweep_timer_->start();
}

void RegistrarActions::stop_binding_sweep_timer() {
    if (binding_sweep_timer_) {
        binding_sweep_timer_->stop();
        binding_sweep_timer_.reset();
    }
}

void RegistrarActions::process_pending_binding_sweep() {
    if (!binding_sweep_timer_) {
        return;
    }
    binding_sweep_timer_->run_pending_scan([this]([[maybe_unused]] std::chrono::steady_clock::duration interval) {
        binding_store_->sweep(std::chrono::steady_clock::now());
    });
}

} // namespace SbcEngine
