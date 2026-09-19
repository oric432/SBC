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


def test_b2bua_caller_prack(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #123: an INVITE that requires 100rel gets reliable provisional
    responses on the caller-facing leg -- the SBC's 180 carries RSeq and
    Require: 100rel, the caller's PRACK is acknowledged, and the call
    completes normally afterward."""
    scenario_name = "caller_prack"
    caller_xml = render_scenario("caller_prack.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee_delayed_200.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_update_before_answer(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #116 / RFC 3311 S5.1: an offer-bearing UPDATE sent before the
    initial INVITE's final response (right after the 180) is rejected --
    this leg's own initial-INVITE offer is still outstanding, and the SBC's
    reliable provisional responses (issue #123) never carry SDP, so that
    precondition can never be satisfied here -- and the original call still
    completes normally afterward."""
    scenario_name = "update_before_answer"
    caller_xml = render_scenario("update_before_answer_caller.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_callee_prack_183(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #123: the callee-facing leg now advertises 100rel, so a callee
    that answers reliably (183 + SDP) gets PRACKed and its early answer is
    staged rather than lost. Issue #214: that answer is relayed to the
    caller as its own 183. The two legs' 100rel negotiations are
    independent, though: caller.xml.j2 never requires 100rel on this leg,
    so PJSIP cannot mark it reliably confirmed and the 200 OK must still
    repeat the answer (RFC 6337) -- see test_b2bua_caller_and_callee_prack
    for the case where the caller leg is reliable too, which is the one
    where the 200 OK must NOT repeat it (RFC 3262 S5)."""
    scenario_name = "callee_prack_183"
    caller_xml = render_scenario("caller.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee_prack_183.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_early_media(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #214: a callee answering with an unreliable 183 + SDP (no
    Require: 100rel -- the production-common shape, see the captured Cisco
    CUCM INVITE) gets that answer relayed to the caller as its own 183, with
    real RTP flowing before the 200 OK. The following 200 OK on both legs
    stays bodiless, and the call completes normally."""
    scenario_name = "early_media"
    caller_xml = render_scenario("caller_early_media.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee_early_media.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_caller_and_callee_prack(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #214: when BOTH legs' 100rel negotiations are reliable -- the
    callee answers reliably (183 + SDP, callee_prack_183.xml.j2) and the
    caller itself requires 100rel too -- the callee's early answer is
    relayed to the caller as its own reliable 183, and because this leg's
    own negotiation is thereby confirmed, the 200 OK must NOT repeat the
    SDP (RFC 3262 S5). This is the one case where a duplicated SDP body
    would actually violate the protocol, unlike test_b2bua_callee_prack_183
    and test_b2bua_early_media, where the caller leg isn't reliable and
    RFC 6337 requires the answer to be repeated."""
    scenario_name = "caller_and_callee_prack"
    caller_xml = render_scenario("caller_prack_early_media.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario("callee_prack_183.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_update_early_media(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #211: a caller that only advertised Supported: 100rel (never
    Require) gets its early answer relayed unreliably on a 183 (#219). The
    SBC's own gate for #211 opens as soon as that early answer went out --
    long before the call is Established -- so an offer-bearing UPDATE right
    after the 183 switches this leg's codec (PCMU -> G722) mid-ringback. The
    INVITE's own final 200 OK must carry that new codec, not replay the 183's
    now-stale answer (see Inv::answer_with_active_local())."""
    scenario_name = "update_early_media"
    caller_xml = render_scenario(
        "update_early_media_caller.xml.j2", scenario_name=scenario_name, g722_pcap_path=_PCAP_PATHS["g722"]
    )
    callee_xml = render_scenario("callee_early_media.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_update_early_media_prack(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #211: a caller that requires 100rel PRACKs the SBC's relayed
    early answer (#123/#214), resolving this leg's own negotiator well before
    the callee has sent a final response to the initial INVITE. An
    offer-bearing UPDATE right after the PRACK switches this leg's codec
    (PCMU -> G722) during that early dialog, and the INVITE's own final 200 OK
    still must not repeat the SDP (RFC 3262 S5), unaffected by the codec
    change in between."""
    scenario_name = "update_early_media_prack"
    caller_xml = render_scenario(
        "update_early_media_prack_caller.xml.j2", scenario_name=scenario_name, g722_pcap_path=_PCAP_PATHS["g722"]
    )
    callee_xml = render_scenario("callee_prack_183.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario_name)


def test_b2bua_update_early_media_callee(sbc_engine, render_scenario, run_sipp_pair):
    """Issue #211: the callee leg accepts an early-dialog UPDATE too, not
    just the caller leg -- the callee itself sends an offer-bearing UPDATE
    (PCMU -> G722) right after PRACKing its own reliable early answer,
    before it has even sent a final response to the initial INVITE. Answered
    locally through the same negotiate_mid_dialog_offer() path, biased
    toward the caller's already-relayed codec, and exercises the relay's
    transcode path (caller stays PCMU, callee switches to G722)."""
    scenario_name = "update_early_media_callee"
    caller_xml = render_scenario("caller.xml.j2", scenario_name=scenario_name)
    callee_xml = render_scenario(
        "callee_update_early_media.xml.j2", scenario_name=scenario_name, g722_pcap_path=_PCAP_PATHS["g722"]
    )

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


# Issue #104: SIP REGISTER (RFC 3261). registered_user comes from
# conftest.py's session-scoped sip_users fixture, seeded into the engine's
# UsersStore before it starts -- these tests exercise the full digest
# challenge/verify path against that real credential, not a mock.


def test_b2bua_register_correct_credentials(sbc_engine, render_scenario, run_sipp_register, registered_user):
    scenario_xml = render_scenario(
        "register.xml.j2",
        scenario_name="register_correct_credentials",
        username=registered_user.username,
        realm=registered_user.realm,
        password=registered_user.password,
        expires=90,
        expected_status=200,
    )

    run_sipp_register(scenario_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during REGISTER:\n{sbc_engine.log_tail()}"
    _log.info("scenario 'register_correct_credentials' passed")


def test_b2bua_register_wrong_password(sbc_engine, render_scenario, run_sipp_register, registered_user):
    scenario_xml = render_scenario(
        "register.xml.j2",
        scenario_name="register_wrong_password",
        username=registered_user.username,
        realm=registered_user.realm,
        password="not-the-real-password",
        expires=90,
        expected_status=403,
    )

    run_sipp_register(scenario_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during REGISTER:\n{sbc_engine.log_tail()}"
    _log.info("scenario 'register_wrong_password' passed")


def test_b2bua_register_expires_too_short(sbc_engine, render_scenario, run_sipp_register, registered_user):
    """Below settings.toml's [registrar] min_expires_s (60 in this suite's
    generated settings, see conftest.py's sbc_engine fixture) -> 423
    Interval Too Brief rather than a shortened grant."""
    scenario_xml = render_scenario(
        "register.xml.j2",
        scenario_name="register_expires_too_short",
        username=registered_user.username,
        realm=registered_user.realm,
        password=registered_user.password,
        expires=10,
        expected_status=423,
    )

    run_sipp_register(scenario_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during REGISTER:\n{sbc_engine.log_tail()}"
    _log.info("scenario 'register_expires_too_short' passed")


def test_b2bua_register_deregister(sbc_engine, render_scenario, run_sipp_register, registered_user):
    """Expires: 0 removes the binding and still gets a 200 OK (RFC 3261
    10.3) -- not a rejection, and not subject to min_expires_s."""
    scenario_xml = render_scenario(
        "register.xml.j2",
        scenario_name="register_deregister",
        username=registered_user.username,
        realm=registered_user.realm,
        password=registered_user.password,
        expires=0,
        expected_status=200,
    )

    run_sipp_register(scenario_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during REGISTER:\n{sbc_engine.log_tail()}"
    _log.info("scenario 'register_deregister' passed")


def test_b2bua_register_wrong_aor_rejected(
    sbc_engine, render_scenario, run_sipp_register, registered_user, other_registered_user
):
    """Authenticating with one user's real credentials must not be able to
    bind a different user's AOR: the digest response only proves the
    requester knows *some* account's password in this realm, not that
    they're entitled to register the AOR named in the To header."""
    scenario_xml = render_scenario(
        "register.xml.j2",
        scenario_name="register_wrong_aor_rejected",
        username=registered_user.username,
        password=registered_user.password,
        to_username=other_registered_user.username,
        realm=registered_user.realm,
        expires=90,
        expected_status=403,
    )

    run_sipp_register(scenario_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during REGISTER:\n{sbc_engine.log_tail()}"
    _log.info("scenario 'register_wrong_aor_rejected' passed")
