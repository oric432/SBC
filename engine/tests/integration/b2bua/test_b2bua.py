from __future__ import annotations

import logging

import pytest

from .conftest import _PCAP_PATHS
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


def test_b2bua_reinvite_new_codec(sbc_engine, render_scenario, run_sipp_pair):
    """A mid-call re-INVITE that switches the caller's codec (PCMU -> G722) is
    answered locally on that leg with the new codec, without being forwarded
    to the callee (which stays on PCMU), and the relay switches to
    transcoding for the rest of the call."""
    scenario_name = "reinvite_new_codec"
    caller_xml = render_scenario(
        "reinvite_new_codec_caller.xml.j2", scenario_name=scenario_name, g722_pcap_path=_PCAP_PATHS["g722"]
    )
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_update_before_answer(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #116 / RFC 3311 S5.1: an offer-bearing UPDATE sent before the
    initial INVITE's final response (right after the 180) is rejected --
    this leg's own initial-INVITE offer is still outstanding, and this SBC
    never orchestrates the 100rel/PRACK exchange RFC 3311 requires before
    such an UPDATE would be valid -- and the original call still completes
    normally afterward."""
    scenario_name = "update_before_answer"
    caller_xml = render_scenario("update_before_answer_caller.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_callee_prack_183(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #123: the callee-facing leg now advertises 100rel, so a callee
    that answers reliably (183 + SDP) gets PRACKed and its early answer is
    staged rather than lost -- the SBC's 200 OK to the caller still carries
    the (only) negotiated answer, and the call completes normally."""
    scenario_name = "callee_prack_183"
    caller_xml = render_scenario("caller.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee_prack_183.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_update_new_codec(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #116: a mid-call UPDATE that switches the caller's codec
    (PCMU -> G722) is answered locally on that leg with the new codec,
    without being forwarded to the callee (which stays on PCMU), and the
    relay switches to transcoding for the rest of the call -- same as
    reinvite_new_codec, but via UPDATE (RFC 3311) instead of re-INVITE."""
    scenario_name = "update_new_codec"
    caller_xml = render_scenario(
        "update_new_codec_caller.xml.j2", scenario_name=scenario_name, g722_pcap_path=_PCAP_PATHS["g722"]
    )
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)
