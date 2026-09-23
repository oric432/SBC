#include "pjsip_init.hpp"

#include <algorithm>
#include <array>
#include <format>

#include <pjlib-util.h>
#include <pjsip-simple/evsub.h>
#include <pjsip_ua.h>
#include <pjsip-ua/sip_xfer.h>

#include "sip/router/message_router.hpp"
#include "core/utils/log.hpp"
#include "net/pj_status_error.hpp"

namespace SbcEngine {

namespace {

constexpr unsigned kEventPollMs = 10;

// One active stack at a time; the PJSIP module callbacks are plain C function
// pointers with no user-data slot, so we recover the stack (and its router)
// through this pointer.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PjsipStack* g_active_stack = nullptr;

// g_active_stack->router(), or nullptr if either isn't set up yet -- every
// callback below hits this same race window (module registered before
// set_router() runs, or torn down before pjsip finishes flushing events).
MessageRouter* active_router() {
    return g_active_stack != nullptr ? g_active_stack->router() : nullptr;
}

// Application module: receives out-of-dialog requests (initial INVITE, OPTIONS,
// and anything without a matching dialog). In-dialog traffic is delivered to the
// invite-session callbacks instead, so this only forwards to the router.
pj_bool_t on_rx_request(pjsip_rx_data* rdata) {
    MessageRouter* router = active_router();
    if (router == nullptr) {
        return PJ_FALSE;
    }
    if (pjsip_rdata_get_dlg(rdata) != nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C event API
        if (rdata->msg_info.msg->line.req.method.id != PJSIP_OTHER_METHOD ||
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C event API
            pj_stricmp2(&rdata->msg_info.msg->line.req.method.name, "REFER") != 0) {
            return PJ_FALSE;
        }
        return PJ_TRUE;
    }
    router->on_rx_request(rdata);
    return PJ_TRUE;
}

void on_tsx_state(pjsip_transaction* tsx, pjsip_event* event) {
    MessageRouter* router = active_router();
    // The TRYING-state restriction ensures the REFER is dispatched once
    if (router == nullptr || tsx->state != PJSIP_TSX_STATE_TRYING || event == nullptr ||
        event->type != PJSIP_EVENT_TSX_STATE) {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C event API
    if (event->body.tsx_state.type != PJSIP_EVENT_RX_MSG) {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C event API
    pjsip_rx_data* request = event->body.tsx_state.src.rdata;
    if (request != nullptr && request->msg_info.msg->type == PJSIP_REQUEST_MSG &&
        pjsip_rdata_get_dlg(request) != nullptr &&
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C event API
        pjsip_method_cmp(&request->msg_info.msg->line.req.method, pjsip_get_refer_method()) == 0) {
        router->on_rx_refer(request);
    }
}

// Invite-session state changes: hand the new state (plus the message that
// caused it, when there is one) to the router for SM event mapping.
void on_inv_state_changed(pjsip_inv_session* inv, pjsip_event* event) {
    Log::sip()->trace("pjsip inv state changed: {}", pjsip_inv_state_name(inv->state));
    MessageRouter* router = active_router();
    if (router == nullptr) {
        return;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) — PJSIP C event API
    pjsip_rx_data* rdata = nullptr;
    if (event != nullptr && event->type == PJSIP_EVENT_TSX_STATE && event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
        rdata = event->body.tsx_state.src.rdata;
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    router->on_inv_state_changed(inv, rdata);
}

void on_inv_new_session(pjsip_inv_session* /*inv*/, pjsip_event* /*e*/) {}

pj_status_t on_rx_reinvite(pjsip_inv_session* inv, const pjmedia_sdp_session* offer, pjsip_rx_data* rdata) {
    MessageRouter* router = active_router();
    if (router == nullptr) {
        return PJ_ENOTFOUND;
    }
    return router->on_rx_reinvite(inv, offer, rdata);
}

// Fires for every new offer pjsip receives (initial INVITE, re-INVITE, and
// UPDATE alike), but the router only acts on UPDATE -- the other two are
// already fully handled via on_rx_reinvite. param->rdata is const here even
// though pjsip's own on_rx_reinvite takes it non-const; this callback never
// mutates it, so the cast is safe.
void on_rx_offer2(pjsip_inv_session* inv, pjsip_inv_on_rx_offer_cb_param* param) {
    MessageRouter* router = active_router();
    if (router == nullptr) {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — PJSIP C API
    router->on_rx_offer(inv, param->offer, const_cast<pjsip_rx_data*>(param->rdata));
}

void on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer) {
    if (MessageRouter* router = active_router(); router != nullptr) {
        router->on_create_offer(inv, offer);
    }
}

void on_inv_media_update(pjsip_inv_session* inv, pj_status_t status) {
    if (MessageRouter* router = active_router(); router != nullptr) {
        router->on_inv_media_update(inv, status);
    }
}

} // namespace

std::string PjsipConfig::own_contact_uri() const {
    return std::format("<sip:{}@{}:{}>", identity_user_, local_ip_, sip_port_);
}

std::string PjsipConfig::caller_facing_from_uri(const std::string& display_name, const std::string& user) const {
    if (display_name.empty()) {
        return std::format("<sip:{}@{}:{}>", user, local_ip_, sip_port_);
    }
    return std::format("\"{}\" <sip:{}@{}:{}>", display_name, user, local_ip_, sip_port_);
}

PjsipStack::~PjsipStack() {
    shutdown();
}

VoidResult PjsipStack::init(const PjsipConfig& config) {
    // Route native PJSIP logging through spdlog before anything can log.
    // Settings::pjsip_log_level controls this (default "disabled" -> 0).
    pj_log_set_level(config.pjsip_log_level_);

    pj_status_t status = pj_init();
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pj_init failed", status));
    }

    status = pjlib_util_init();
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjlib_util_init failed", status));
    }

    pj_caching_pool_init(&caching_pool_, &pj_pool_factory_default_policy, 0);

    status = pjsip_endpt_create(&caching_pool_.factory, "sbc", &endpt_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_endpt_create failed", status));
    }

    if (auto res = start_transport(config); !res) {
        return res;
    }

    status = pjsip_tsx_layer_init_module(endpt_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_tsx_layer_init_module failed", status));
    }

    // t1/t2/t4 left at PJSIP defaults (0 = unchanged); td is the INVITE
    // transaction's completion timeout — this is what fires cause 408 when a
    // callee never answers.
    pjsip_tsx_set_timers(0, 0, 0, static_cast<unsigned>(config.invite_timeout_ms_));

    status = pjsip_ua_init_module(endpt_, nullptr);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_ua_init_module failed", status));
    }

    pjsip_inv_callback inv_cb;
    pj_bzero(&inv_cb, sizeof(inv_cb));
    inv_cb.on_state_changed = &on_inv_state_changed;
    inv_cb.on_new_session = &on_inv_new_session;
    inv_cb.on_rx_reinvite = &on_rx_reinvite;
    inv_cb.on_rx_offer2 = &on_rx_offer2;
    inv_cb.on_create_offer = &on_create_offer;
    inv_cb.on_media_update = &on_inv_media_update;

    status = pjsip_inv_usage_init(endpt_, &inv_cb);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_inv_usage_init failed", status));
    }

    status = pjsip_timer_init_module(endpt_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_timer_init_module failed", status));
    }

    // The invite session attaches mod-100rel when creating UAC sessions and
    // asserts if it was never registered, so it must be initialized even
    // though we do not orchestrate PRACK ourselves.
    status = pjsip_100rel_init_module(endpt_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_100rel_init_module failed", status));
    }

    status = pjsip_evsub_init_module(endpt_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_evsub_init_module failed", status));
    }

    status = pjsip_xfer_init_module(endpt_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_xfer_init_module failed", status));
    }

    static std::string mod_name = "mod-sbc";
    pj_bzero(&module_, sizeof(module_));
    module_.name = pj_str(mod_name.data());
    module_.id = -1;
    module_.priority = PJSIP_MOD_PRIORITY_APPLICATION;
    module_.on_rx_request = &on_rx_request;
    module_.on_tsx_state = &on_tsx_state;

    status = pjsip_endpt_register_module(endpt_, &module_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_endpt_register_module failed", status));
    }

    // pjsip_inv_usage_init() already registered INVITE/ACK/BYE/CANCEL/UPDATE
    // (plus PRACK via 100rel) in the endpoint's Allow capability; add OPTIONS
    // and REGISTER so responses built from that capability (see
    // OptionsActions, RegistrarActions) advertise them too, per RFC 3261.
    static std::array<std::string, 2> extra_methods = {"OPTIONS", "REGISTER"};
    std::array<pj_str_t, extra_methods.size()> extra_method_tags{};
    std::ranges::transform(extra_methods, extra_method_tags.begin(), [](std::string& method) {
        return pj_str(method.data());
    });
    status = pjsip_endpt_add_capability(
        endpt_,
        &module_,
        PJSIP_H_ALLOW,
        nullptr,
        extra_method_tags.size(),
        extra_method_tags.data());
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_endpt_add_capability failed", status));
    }

    g_active_stack = this;
    initialized_ = true;
    Log::sip()->info("PJSIP stack initialized, listening on {}:{}", config.bind_ip_, config.sip_port_);
    return {};
}

VoidResult PjsipStack::start_transport(const PjsipConfig& config) {
    pj_sockaddr_in addr;
    pj_bzero(&addr, sizeof(addr));
    addr.sin_family = pj_AF_INET();
    addr.sin_port = pj_htons(config.sip_port_);

    if (config.bind_ip_ != "0.0.0.0") {
        std::string bind = config.bind_ip_;
        const pj_str_t bind_addr = pj_str(bind.data());
        const pj_status_t addr_status = pj_sockaddr_in_set_str_addr(&addr, &bind_addr);
        if (addr_status != PJ_SUCCESS) {
            return std::unexpected(pj_error("invalid bind_ip", addr_status));
        }
    }

    const pj_status_t status = pjsip_udp_transport_start(endpt_, &addr, nullptr, 1, nullptr);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_udp_transport_start failed", status));
    }
    return {};
}

void PjsipStack::run() {
    if (!initialized_) {
        return;
    }
    running_ = true;
    while (running_) {
        const pj_time_val timeout = {.sec = 0, .msec = kEventPollMs};
        pjsip_endpt_handle_events(endpt_, &timeout);
        if (router_ != nullptr) {
            router_->process_pending_media_events();
        }
    }
}

void PjsipStack::stop() {
    running_ = false;

    // Not thread-safe. Only OK because today's only caller is the signal
    // handler, same thread as run().
    router_ = nullptr;
}

void PjsipStack::shutdown() {
    if (!initialized_) {
        return;
    }
    initialized_ = false;
    running_ = false;

    if (endpt_ != nullptr) {
        pjsip_endpt_destroy(endpt_);
        endpt_ = nullptr;
    }
    pj_caching_pool_destroy(&caching_pool_);
    pj_shutdown();

    if (g_active_stack == this) {
        g_active_stack = nullptr;
    }
    Log::sip()->info("PJSIP stack shut down");
}

} // namespace SbcEngine
