#pragma once

#include <optional>
#include <string>
#include <vector>

#include <pjsip.h>

#include "sip/call/pj_context.hpp"
#include "sip/registrar/binding_store.hpp"
#include "sip/registrar/i_registration_sink.hpp"
#include "sip/registrar/users_store.hpp"

namespace SbcEngine {

struct RegistrarConfig {
    int min_expires_s_;
    int max_expires_s_;
};

// Stateless REGISTER responder (the SBC's registrar role): digest-challenges
// against UsersStore's credentials, applies the result to BindingStore, and
// answers. Not tied to a call, like OptionsActions -- the router points it
// at the current request before handling it.
//
// No SML machine: the challenge is stateless (a self-validating nonce) and
// both request/response halves are independent transactions with no async
// wait between them, unlike setup or dialog. A machine here would be
// transition-table boilerplate around straight-line code -- a deliberate
// deviation from the sm/ convention.
//
// Holds PjContext* (not pjsip_endpoint* directly) and reads ctx_->endpt_
// lazily: MessageRouter -- and therefore this object -- is constructed in
// SbcApp's member-init list, before SbcApp::init() populates ctx_.endpt_
// (see OptionsActions for the same requirement). config_ is a pointer to
// SbcApp's own member for the identical reason -- it isn't populated until
// well into SbcApp::init(), well after this object is constructed, but it's
// always valid by the time a REGISTER actually arrives. sink_ starts null
// and is wired up later still, via set_registration_sink() --
// ControlPlaneClient itself isn't constructed until after MessageRouter (and
// the RegistrarActions it owns) already is.
class RegistrarActions {
public:
    RegistrarActions(PjContext* ctx, UsersStore* users_store, BindingStore* binding_store, RegistrarConfig* config);

    void handle(pjsip_rx_data* rdata);
    void set_registration_sink(IRegistrationSink* sink) { sink_ = sink; }

private:
    // The To-header's host, if it's a domain UsersStore currently knows
    // about (i.e. some enabled user's realm) -- else nullopt, in which case
    // the caller responds 404 without ever touching auth.
    [[nodiscard]] std::optional<std::string> local_domain(pjsip_rx_data* rdata) const;
    // PJ_SUCCESS if authenticated. PJSIP_EAUTHNOAUTH means "send a challenge"
    // (the caller does that); any other non-success is a rejection, with
    // *status_code already filled in by pjsip.
    static pj_status_t verify(pjsip_rx_data* rdata, const std::string& realm, int* status_code);
    // The username the digest response actually authenticated, straight from
    // the request's own Authorization header -- pjsip_auth_srv_verify() only
    // proves the response matches some credential set named in that header,
    // never that it matches the AOR the request is trying to register.
    static std::optional<std::string> authenticated_username(pjsip_rx_data* rdata);
    void send_challenge(pjsip_rx_data* rdata, const std::string& realm);
    void process_registration(pjsip_rx_data* rdata, const std::string& realm, const std::string& to_user);
    // True (having sent 423 + Min-Expires) if some contact's requested expiry
    // is below config_->min_expires_s_. Expires: 0 (de-registration) is
    // exempt -- the floor doesn't apply to it.
    bool reject_if_too_brief(
        pjsip_rx_data* rdata,
        const std::vector<pjsip_contact_hdr*>& contacts,
        std::optional<int> top_level_expires);
    void respond(pjsip_rx_data* rdata, int status_code);
    // 200 OK listing every live binding currently on file for `aor`.
    void send_ok(pjsip_rx_data* rdata, const std::string& aor);
    void mirror_registration(
        const std::string& aor,
        const Binding& binding,
        bool removed,
        const std::optional<std::string>& user_agent);

    PjContext* ctx_;
    UsersStore* users_store_;
    BindingStore* binding_store_;
    IRegistrationSink* sink_ = nullptr;
    RegistrarConfig* config_;
};

} // namespace SbcEngine
