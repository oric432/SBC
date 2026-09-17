#pragma once

#include <memory>
#include <optional>
#include <string>

#include <pjsip.h>

#include "sip/call/pj_context.hpp"
#include "sip/registrar/binding_store.hpp"
#include "sip/registrar/users_store.hpp"

namespace SbcEngine {

class ControlPlaneClient;

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
// (see OptionsActions for the same requirement). control_plane_client_ and
// config_ are pointers to SbcApp's own members for the identical reason --
// neither is populated until well into SbcApp::init(), well after this
// object is constructed, but both are always valid by the time a REGISTER
// actually arrives.
class RegistrarActions {
public:
    RegistrarActions(
        PjContext* ctx,
        UsersStore* users_store,
        BindingStore* binding_store,
        std::shared_ptr<ControlPlaneClient>* control_plane_client,
        RegistrarConfig* config);

    void handle(pjsip_rx_data* rdata);

private:
    // The To-header's host, if it's a domain UsersStore currently knows
    // about (i.e. some enabled user's realm) -- else nullopt, in which case
    // the caller responds 404 without ever touching auth.
    [[nodiscard]] std::optional<std::string> local_domain(pjsip_rx_data* rdata) const;
    // PJ_SUCCESS if authenticated. PJSIP_EAUTHNOAUTH means "send a challenge"
    // (the caller does that); any other non-success is a rejection, with
    // *status_code already filled in by pjsip.
    static pj_status_t verify(pjsip_rx_data* rdata, const std::string& realm, int* status_code);
    void send_challenge(pjsip_rx_data* rdata, const std::string& realm);
    void process_registration(pjsip_rx_data* rdata, const std::string& realm, const std::string& to_user);
    void respond(pjsip_rx_data* rdata, int status_code);
    // 200 OK listing every live binding currently on file for `aor`.
    void send_ok(pjsip_rx_data* rdata, const std::string& aor);
    void mirror_registration(const std::string& aor, const Binding& binding, bool removed);

    PjContext* ctx_;
    UsersStore* users_store_;
    BindingStore* binding_store_;
    std::shared_ptr<ControlPlaneClient>* control_plane_client_;
    RegistrarConfig* config_;
};

} // namespace SbcEngine
