#pragma once

#include <string>

namespace Sbc {

// Forward declarations
struct pjsip_rx_data;
class CallSession;
class RealSetupActions;
class RealDialogActions;

class MessageRouter {
public:
    explicit MessageRouter(RealSetupActions* setup_actions, RealDialogActions* dialog_actions)
        : setup_actions_(setup_actions)
        , dialog_actions_(dialog_actions) {}

    // Main entry point: called by PJSIP callback for all incoming requests
    void on_rx_request(pjsip_rx_data* rx_data);

private:
    RealSetupActions* setup_actions_;
    RealDialogActions* dialog_actions_;

    // Stateless message handlers (OPTIONS, INFO, etc.)
    void process_options(pjsip_rx_data* rx_data);
    void process_info(pjsip_rx_data* rx_data);

    // Stateful message handlers (INVITE, BYE, CANCEL, ACK, etc.)
    void process_invite(pjsip_rx_data* rx_data);
    void process_bye(pjsip_rx_data* rx_data);
    void process_cancel(pjsip_rx_data* rx_data);
    void process_ack(pjsip_rx_data* rx_data);

    // Helper functions
    std::string extract_method(pjsip_rx_data* rx_data);
    std::string extract_sdp(pjsip_rx_data* rx_data);
    CallSession* find_call_session(pjsip_rx_data* rx_data);
    bool is_from_caller(pjsip_rx_data* rx_data, CallSession* call_session);

    void send_481_call_does_not_exist(pjsip_rx_data* rx_data);
    void send_405_method_not_allowed(pjsip_rx_data* rx_data);
};

} // namespace Sbc
