#include "pjsip_init.hpp"

#include <format>

#include <pjlib-util.h>
#include <pjsip_ua.h>

#include "router/message_router.hpp"
#include "utils/log.hpp"

using namespace SIPI;

namespace SbcEngine {

namespace {

constexpr unsigned kEventPollMs = 10;

// One active stack at a time; the PJSIP module callbacks are plain C function
// pointers with no user-data slot, so we recover the stack (and its router)
// through this pointer.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PjsipStack* g_active_stack = nullptr;

std::string pj_status_str(pj_status_t status) {
    std::array<char, PJ_ERR_MSG_SIZE> buf{};
    pj_strerror(status, buf.data(), buf.size());
    return {buf.data()};
}

Error pj_error(const std::string& what, pj_status_t status) {
    return Error("{}: {}", what, pj_status_str(status));
}

// Application module: receives out-of-dialog requests (initial INVITE, OPTIONS,
// and anything without a matching dialog). In-dialog traffic is delivered to the
// invite-session callbacks instead, so this only forwards to the router.
pj_bool_t on_rx_request(pjsip_rx_data* rdata) {
    if (g_active_stack == nullptr || g_active_stack->router() == nullptr) {
        return PJ_FALSE;
    }
    g_active_stack->router()->on_rx_request(rdata);
    return PJ_TRUE;
}

// Invite-session state changes: hand the new state (plus the message that
// caused it, when there is one) to the router for SM event mapping.
void on_inv_state_changed(pjsip_inv_session* inv, pjsip_event* event) {
    Log::sip()->debug("inv state changed: {}", pjsip_inv_state_name(inv->state));
    if (g_active_stack == nullptr || g_active_stack->router() == nullptr) {
        return;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) — PJSIP C event API
    pjsip_rx_data* rdata = nullptr;
    if (event != nullptr && event->type == PJSIP_EVENT_TSX_STATE && event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
        rdata = event->body.tsx_state.src.rdata;
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    g_active_stack->router()->on_inv_state_changed(inv, rdata);
}

void on_inv_new_session(pjsip_inv_session* /*inv*/, pjsip_event* /*e*/) {}

void on_inv_media_update(pjsip_inv_session* /*inv*/, pj_status_t /*status*/) {}

} // namespace

std::string PjsipConfig::own_contact_uri() const {
    return std::format("<sip:{}@{}:{}>", identity_user_, local_ip_, sip_port_);
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

    status = pjsip_ua_init_module(endpt_, nullptr);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_ua_init_module failed", status));
    }

    pjsip_inv_callback inv_cb;
    pj_bzero(&inv_cb, sizeof(inv_cb));
    inv_cb.on_state_changed = &on_inv_state_changed;
    inv_cb.on_new_session = &on_inv_new_session;
    inv_cb.on_media_update = &on_inv_media_update;

    status = pjsip_inv_usage_init(endpt_, &inv_cb);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_inv_usage_init failed", status));
    }

    // The invite session attaches mod-100rel when creating UAC sessions and
    // asserts if it was never registered, so it must be initialized even
    // though we do not orchestrate PRACK ourselves.
    status = pjsip_100rel_init_module(endpt_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_100rel_init_module failed", status));
    }

    static std::string mod_name = "mod-sbc";
    pj_bzero(&module_, sizeof(module_));
    module_.name = pj_str(mod_name.data());
    module_.id = -1;
    module_.priority = PJSIP_MOD_PRIORITY_APPLICATION;
    module_.on_rx_request = &on_rx_request;

    status = pjsip_endpt_register_module(endpt_, &module_);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjsip_endpt_register_module failed", status));
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
        pj_str_t bind_addr = pj_str(bind.data());
        pj_status_t addr_status = pj_sockaddr_in_set_str_addr(&addr, &bind_addr);
        if (addr_status != PJ_SUCCESS) {
            return std::unexpected(pj_error("invalid bind_ip", addr_status));
        }
    }

    pj_status_t status = pjsip_udp_transport_start(endpt_, &addr, nullptr, 1, nullptr);
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
        pj_time_val timeout = {0, kEventPollMs};
        pjsip_endpt_handle_events(endpt_, &timeout);
    }
}

void PjsipStack::stop() {
    running_ = false;
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

} // namespace SbcEngineEngine
