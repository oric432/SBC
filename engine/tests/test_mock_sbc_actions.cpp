#include <catch2/catch_test_macros.hpp>

#include "mock_sbc_actions.hpp"

using namespace SbcEngine;

TEST_CASE("Mock setup actions record calls and reset", "[sbc_mock]") {
    MockSetupActions actions;

    REQUIRE(actions.resolve_route().kind_ == RouteResolution::Kind::kFound);
    REQUIRE(actions.create_outbound_leg("sip:callee@example.com"));
    REQUIRE(actions.was_called("resolve_route"));
    REQUIRE(actions.was_called("create_outbound_leg:sip:callee@example.com"));

    actions.reset();
    REQUIRE_FALSE(actions.was_called("resolve_route"));
}

TEST_CASE("Mock dialog and options actions record calls", "[sbc_mock]") {
    MockDialogActions dialog_actions;
    dialog_actions.forward_reinvite("v=0\r\n");
    REQUIRE(dialog_actions.was_called("forward_reinvite:5B"));

    MockOptionsActions options_actions;
    options_actions.send_options_response();
    REQUIRE(options_actions.was_called("send_options_response"));
}
