from __future__ import annotations

import logging

import pytest

from .scenarios import SCENARIOS

_log = logging.getLogger("integration.b2bua")


@pytest.mark.parametrize("scenario", SCENARIOS, ids=lambda s: s.name)
def test_b2bua_call(sbc_engine, render_scenario, run_sipp_pair, scenario):
    total_hold_s = sum(scenario.holds_ms) / 1000
    _log.info("running scenario '%s' (~%.0fs call)...", scenario.name, total_hold_s)

    caller_xml = render_scenario(
        scenario.caller_template, scenario_name=scenario.name, holds_ms=scenario.holds_ms, pcap=scenario.caller_pcap
    )
    callee_xml = render_scenario(scenario.callee_template, scenario_name=scenario.name, pcap=scenario.callee_pcap)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario.name)


def test_b2bua_reinvite_same_sdp(sbc_engine, render_scenario, run_sipp_pair):
    """A mid-call re-INVITE carrying the same SDP is answered locally on that
    leg (200 OK) without being forwarded to the other leg."""
    scenario_name = "reinvite_same_sdp"
    caller_xml = render_scenario("reinvite_same_sdp_caller.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_reinvite_offerless(sbc_engine, render_scenario, run_sipp_pair):
    """A mid-call re-INVITE with no SDP body gets an SBC-generated offer in
    the 200 OK, and the caller's ACK carries the answer."""
    scenario_name = "reinvite_offerless"
    caller_xml = render_scenario("reinvite_offerless_caller.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_reinvite_new_port(sbc_engine, render_scenario, run_sipp_pair):
    """A mid-call re-INVITE that keeps the same codec but moves the caller's
    own RTP port is answered locally on that leg (200 OK) with the relay
    retargeted, without being forwarded to the callee."""
    scenario_name = "reinvite_new_port"
    caller_xml = render_scenario("reinvite_new_port_caller.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)
