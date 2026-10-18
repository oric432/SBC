from __future__ import annotations

import logging
import re
import socket
import struct
import threading
import time
import urllib.parse
import uuid
from pathlib import Path

import pytest

from .conftest import _PCAP_PATHS
from .scenarios import SCENARIOS

_log = logging.getLogger("integration.b2bua")


def _assert_replaces_translated_to_callee_dialog(message_trace: str) -> None:
    invite = re.search(r"INVITE sip:.*? SIP/2\.0(?P<headers>.*?)(?:\r?\n){2}", message_trace, re.DOTALL)
    invite_ok = re.search(r"SIP/2\.0 200 OK(?P<headers>.*?)(?:\r?\n){2}", message_trace, re.DOTALL)
    refer = re.search(r"REFER sip:.*? SIP/2\.0(?P<headers>.*?)(?:\r?\n){2}", message_trace, re.DOTALL)
    assert invite is not None and invite_ok is not None and refer is not None

    call_id = re.search(r"^Call-ID:\s*(\S+)", invite["headers"], re.MULTILINE)
    from_tag = re.search(r"^From:.*?;tag=([^;\s>]+)", invite["headers"], re.MULTILINE)
    to_tag = re.search(r"^To:.*?;tag=([^;\s>]+)", invite_ok["headers"], re.MULTILINE)
    replaces = re.search(r"^Refer-To:.*?[?&]Replaces=([^>\s&]+)", refer["headers"], re.MULTILINE)
    assert call_id is not None and from_tag is not None and to_tag is not None and replaces is not None

    translated = re.fullmatch(
        r"([^;]+);to-tag=([^;]+);from-tag=([^;]+)", urllib.parse.unquote(replaces.group(1))
    )
    assert translated is not None
    assert translated.groups() == (call_id.group(1), to_tag.group(1), from_tag.group(1))


def _make_rtp_probe_pcap(source: Path, destination: Path, ssrc: int) -> None:
    """Make a four-packet, uniquely tagged probe from the checked-in capture."""
    data = source.read_bytes()
    if data[:4] == b"\xd4\xc3\xb2\xa1":
        endian = "<"
    elif data[:4] == b"\xa1\xb2\xc3\xd4":
        endian = ">"
    else:
        raise ValueError(f"unsupported PCAP byte order in {source}")

    link_type = struct.unpack_from(f"{endian}I", data, 20)[0]
    if link_type != 1:
        raise ValueError(f"expected an Ethernet PCAP, got link type {link_type}")

    output = bytearray(data[:24])
    offset = 24
    rtp_offset = 14 + 20 + 8  # Ethernet + IPv4 + UDP headers in g711a.pcap.
    for _ in range(4):
        record_header = data[offset : offset + 16]
        if len(record_header) != 16:
            raise ValueError(f"expected four packets in {source}")
        _, _, packet_length, _ = struct.unpack(f"{endian}IIII", record_header)
        packet_start = offset + 16
        packet = bytearray(data[packet_start : packet_start + packet_length])
        if len(packet) != packet_length:
            raise ValueError(f"truncated packet in {source}")

        # This fixture is Ethernet/IPv4/UDP and the scenario negotiates PCMU.
        if packet[12:14] != b"\x08\x00" or packet[14] != 0x45 or packet[23] != 17:
            raise ValueError(f"unexpected packet layout in {source}")
        if packet[rtp_offset] >> 6 != 2:
            raise ValueError(f"expected RTP in {source}")

        # The fixture carries PCMA (PT 8); these probes only test packet flow.
        packet[rtp_offset + 1] &= 0x80  # Keep marker bit, set payload type to PCMU (PT 0).
        packet[rtp_offset + 8 : rtp_offset + 12] = struct.pack(">I", ssrc)
        output.extend(record_header)
        output.extend(packet)
        offset = packet_start + packet_length

    destination.write_bytes(output)


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


@pytest.mark.parametrize("ack_direction", ["sendrecv", "sendonly", "recvonly", "inactive"])
@pytest.mark.parametrize("hold_first", [False, True], ids=["active-call", "resume-after-sendonly-hold"])
@pytest.mark.parametrize("ack_zero_address", [False, True], ids=["normal-address", "zero-address"])
def test_b2bua_reinvite_offerless(
    sbc_engine, render_scenario, run_sipp_pair, ack_direction, hold_first, ack_zero_address, tmp_path
):
    """A mid-call re-INVITE with no SDP body gets an SBC-generated offer in
    the 200 OK, and the caller's ACK carries the answer."""
    scenario_name = "reinvite_offerless"
    probe_ssrc = 0xA0000010
    probe_path = tmp_path / "ack_direction.pcap"
    _make_rtp_probe_pcap(_PCAP_PATHS["g711a"], probe_path, probe_ssrc)
    hold_ssrc, resume_ssrc = 0xB0000010, 0xB0000011
    hold_probe, resume_probe = tmp_path / "hold.pcap", tmp_path / "resume.pcap"
    _make_rtp_probe_pcap(_PCAP_PATHS["g711a"], hold_probe, hold_ssrc)
    _make_rtp_probe_pcap(_PCAP_PATHS["g711a"], resume_probe, resume_ssrc)
    with (
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sink,
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as caller_sink,
    ):
        sink.bind(("127.0.0.1", 0))
        sink.setblocking(False)
        caller_sink.bind(("127.0.0.1", 0))
        caller_sink.setblocking(False)
        caller_xml = render_scenario(
            "reinvite_offerless_caller.xml.j2",
            scenario_name=scenario_name,
            ack_direction=ack_direction,
            probe_pcap_path=probe_path,
            hold_first=hold_first,
            ack_zero_address=ack_zero_address,
            caller_rtp_sink_port=caller_sink.getsockname()[1],
        )
        callee_xml = render_scenario(
            "callee.xml.j2",
            scenario_name=scenario_name,
            rtp_sink_port=sink.getsockname()[1],
            timed_probes=[(3500, hold_probe), (1500, resume_probe)] if hold_first else [(2500, resume_probe)],
        )
        run_sipp_pair(caller_xml, callee_xml, trace=True)
        received = set()
        while True:
            try:
                packet, _ = sink.recvfrom(2048)
            except BlockingIOError:
                break
            if len(packet) >= 12 and packet[0] >> 6 == 2:
                received.add(struct.unpack_from(">I", packet, 8)[0])
        caller_received = set()
        while True:
            try:
                packet, _ = caller_sink.recvfrom(2048)
            except BlockingIOError:
                break
            if len(packet) >= 12 and packet[0] >> 6 == 2:
                caller_received.add(struct.unpack_from(">I", packet, 8)[0])
    assert (probe_ssrc in received) == (not ack_zero_address and ack_direction in {"sendrecv", "sendonly"}), (
        f"incorrect RTP forwarding after {ack_direction} answer in ACK"
    )
    assert hold_ssrc not in caller_received, "sendonly hold RTP incorrectly reached the caller"
    assert (resume_ssrc in caller_received) == (not ack_zero_address and ack_direction in {"sendrecv", "recvonly"}), (
        f"incorrect RTP forwarding toward caller after {ack_direction} answer in ACK"
    )

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


@pytest.mark.parametrize(
    ("legacy_zero_address", "hold_direction", "hold_answer_direction"),
    [
        pytest.param(False, "sendonly", "recvonly", id="cisco-sendonly-hold-resume"),
        pytest.param(True, "sendonly", "inactive", id="cisco-legacy-zero-address-hold-resume"),
    ],
)
def test_b2bua_reinvite_hold_directions(
    sbc_engine, render_scenario, run_sipp_pair, legacy_zero_address, hold_direction, hold_answer_direction, tmp_path
):
    """Cisco-style sendonly/legacy zero-address hold and RFC 3264 direction
    inversion are answered locally; recvonly, inactive, and sendrecv resume
    are checked for both SDP answers and RTP relay gating."""
    scenario_name = "reinvite_hold_directions"
    probe_ssrcs = {
        "caller_sendonly": 0xA0000001,
        "caller_recvonly": 0xA0000002,
        "caller_inactive": 0xA0000003,
        "caller_resume": 0xA0000004,
        "callee_sendonly": 0xB0000001,
        "callee_recvonly": 0xB0000002,
        "callee_inactive": 0xB0000003,
        "callee_resume": 0xB0000004,
    }
    probe_pcaps = {}
    for name, ssrc in probe_ssrcs.items():
        probe_path = tmp_path / f"{name}.pcap"
        _make_rtp_probe_pcap(_PCAP_PATHS["g711a"], probe_path, ssrc)
        probe_pcaps[name] = probe_path

    with (
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as callee_rtp_sink,
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as caller_rtp_sink,
    ):
        callee_rtp_sink.bind(("127.0.0.1", 0))
        caller_rtp_sink.bind(("127.0.0.1", 0))
        callee_rtp_sink.setblocking(False)
        caller_rtp_sink.setblocking(False)
        caller_xml = render_scenario(
            "reinvite_hold_directions_caller.xml.j2",
            scenario_name=scenario_name,
            legacy_zero_address=legacy_zero_address,
            hold_direction=hold_direction,
            hold_answer_direction=hold_answer_direction,
            caller_rtp_sink_port=caller_rtp_sink.getsockname()[1],
            sendonly_pcap_path=probe_pcaps["caller_sendonly"],
            recvonly_pcap_path=probe_pcaps["caller_recvonly"],
            inactive_pcap_path=probe_pcaps["caller_inactive"],
            resume_pcap_path=probe_pcaps["caller_resume"],
        )
        callee_xml = render_scenario(
            "callee_signaling_only.xml.j2",
            scenario_name=scenario_name,
            rtp_sink_port=callee_rtp_sink.getsockname()[1],
            sendonly_pcap_path=probe_pcaps["callee_sendonly"],
            recvonly_pcap_path=probe_pcaps["callee_recvonly"],
            inactive_pcap_path=probe_pcaps["callee_inactive"],
            resume_pcap_path=probe_pcaps["callee_resume"],
        )
        run_sipp_pair(caller_xml, callee_xml, trace=True)

        def received_ssrcs(rtp_sink: socket.socket) -> set[int]:
            received = set()
            while True:
                try:
                    packet, _ = rtp_sink.recvfrom(2048)
                except BlockingIOError:
                    break
                if len(packet) >= 12 and packet[0] >> 6 == 2:
                    received.add(struct.unpack_from(">I", packet, 8)[0])
            return received

        callee_received = received_ssrcs(callee_rtp_sink)
        caller_received = received_ssrcs(caller_rtp_sink)

    if legacy_zero_address:
        assert probe_ssrcs["caller_sendonly"] not in callee_received, "legacy zero-address hold RTP reached the callee"
    else:
        assert probe_ssrcs["caller_sendonly"] in callee_received, "sendonly RTP was not relayed to the callee"
    assert probe_ssrcs["caller_recvonly"] not in callee_received, "recvonly RTP incorrectly reached the callee"
    assert probe_ssrcs["caller_inactive"] not in callee_received, "inactive RTP incorrectly reached the callee"
    assert probe_ssrcs["caller_resume"] in callee_received, "sendrecv RTP was not relayed after resume"

    assert probe_ssrcs["callee_sendonly"] not in caller_received, "sendonly hold RTP incorrectly reached the caller"
    assert probe_ssrcs["callee_recvonly"] in caller_received, "recvonly RTP was not relayed to the caller"
    assert probe_ssrcs["callee_inactive"] not in caller_received, "inactive RTP incorrectly reached the caller"
    assert probe_ssrcs["callee_resume"] in caller_received, "sendrecv RTP was not relayed to the caller after resume"

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


def test_b2bua_refer_rejected_by_peer(sbc_engine, render_scenario, run_sipp_pair):
    scenario_name = "refer_rejected_by_peer"
    caller_xml = render_scenario(
        "refer_rejected_caller.xml.j2",
        scenario_name=scenario_name,
        holds_ms=[1000],
        expected_refer_status=486,
        send_colliding_refer=False,
        bye_during_refer=False,
    )
    callee_xml = render_scenario(
        "refer_rejected_callee.xml.j2", scenario_name=scenario_name, peer_accepts=False
    )

    callee_messages = run_sipp_pair(caller_xml, callee_xml, trace_callee_messages=True)

    assert callee_messages is not None
    _assert_replaces_translated_to_callee_dialog(callee_messages)
    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"


@pytest.mark.parametrize("send_colliding_refer", [False, True], ids=["accepted", "busy_collision"])
def test_b2bua_refer_accepted(sbc_engine, render_scenario, run_sipp_pair, send_colliding_refer):
    scenario_name = "refer_busy_collision" if send_colliding_refer else "refer_accepted_by_peer"
    caller_xml = render_scenario(
        "refer_rejected_caller.xml.j2",
        scenario_name=scenario_name,
        holds_ms=[1000],
        expected_refer_status=200,
        send_colliding_refer=send_colliding_refer,
        bye_during_refer=False,
    )
    callee_xml = render_scenario(
        "refer_rejected_callee.xml.j2",
        scenario_name=scenario_name,
        peer_accepts=True,
        final_notify_delay_ms=1000 if send_colliding_refer else 100,
        notify_status=200,
        bye_during_refer=False,
    )

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"


@pytest.mark.parametrize(
    ("case", "refer_to_value", "expected_status"),
    [
        ("missing", None, 400),
        ("malformed_uri", "not-a-sip-uri", 400),
        ("garbled_replaces", "<sip:transfer@example.com?Replaces=call%ZZid>", 400),
        (
            "unknown_replaces_dialog",
            "<sip:transfer@example.com?Replaces=missing%3Bto-tag%3Dmissing%3Bfrom-tag%3Dmissing>",
            481,
        ),
    ],
)
def test_b2bua_refer_invalid_refer_to(
    sbc_engine, render_scenario, run_sipp_pair, case, refer_to_value, expected_status
):
    scenario_name = f"refer_{case}"
    caller_xml = render_scenario(
        "refer_invalid_caller.xml.j2",
        scenario_name=scenario_name,
        refer_to_value=refer_to_value,
        expected_status=expected_status,
    )
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario_name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"


def test_b2bua_refer_failure_notify(sbc_engine, render_scenario, run_sipp_pair):
    scenario_name = "refer_failure_notify"
    caller_xml = render_scenario(
        "refer_rejected_caller.xml.j2",
        scenario_name=scenario_name,
        holds_ms=[1000],
        expected_refer_status=503,
        send_colliding_refer=False,
        bye_during_refer=False,
    )
    callee_xml = render_scenario(
        "refer_rejected_callee.xml.j2",
        scenario_name=scenario_name,
        peer_accepts=True,
        final_notify_delay_ms=100,
        notify_status=503,
        bye_during_refer=False,
    )

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"


def test_b2bua_bye_during_refer(sbc_engine, render_scenario, run_sipp_pair):
    scenario_name = "bye_during_refer"
    caller_xml = render_scenario(
        "refer_rejected_caller.xml.j2",
        scenario_name=scenario_name,
        holds_ms=[],
        expected_refer_status=200,
        send_colliding_refer=False,
        bye_during_refer=True,
    )
    callee_xml = render_scenario(
        "refer_rejected_callee.xml.j2",
        scenario_name=scenario_name,
        peer_accepts=True,
        final_notify_delay_ms=0,
        notify_status=200,
        bye_during_refer=True,
    )

    run_sipp_pair(caller_xml, callee_xml)

    assert "REFER failed" in sbc_engine.log_tail()
    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"


def test_b2bua_refer_out_of_dialog(sbc_engine, render_scenario, run_sipp_client):
    scenario_xml = render_scenario("refer_out_of_dialog.xml.j2", scenario_name="refer_out_of_dialog")

    run_sipp_client(scenario_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during REFER:\n{sbc_engine.log_tail()}"


def _raw_sdp(port: int) -> str:
    return (
        "v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\n"
        f"m=audio {port} RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n"
    )


def _header_line(message: str, name: str) -> str:
    return re.search(rf"^{name}:.*$", message, re.MULTILINE | re.IGNORECASE).group(0).rstrip()


class _RawUas:
    """UDP callee that answers every INVITE with `status` (a 200 carries SDP) and every BYE with 200."""

    def __init__(self, port: int, status: int = 200) -> None:
        self._status = status
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind(("127.0.0.1", port))
        self._sock.settimeout(0.2)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)
        self._sock.close()

    def _serve(self) -> None:
        while not self._stop.is_set():
            try:
                data, peer = self._sock.recvfrom(65535)
            except OSError:
                continue
            request = data.decode(errors="replace")
            method = request.split(" ", 1)[0]
            if method == "INVITE":
                self._sock.sendto(self._reply(request, self._status), peer)
            elif method == "BYE":
                self._sock.sendto(self._reply(request, 200), peer)

    def _reply(self, request: str, status: int) -> bytes:
        to_line = _header_line(request, "To")
        if ";tag=" not in to_line:
            to_line += f";tag=uas{uuid.uuid4().hex[:8]}"
        port = self._sock.getsockname()[1]
        lines = [
            f"SIP/2.0 {status} {'OK' if status == 200 else 'Rejected'}",
            *(via.rstrip() for via in re.findall(r"^Via:.*$", request, re.MULTILINE | re.IGNORECASE)),
            _header_line(request, "From"),
            to_line,
            _header_line(request, "Call-ID"),
            _header_line(request, "CSeq"),
            f"Contact: <sip:uas@127.0.0.1:{port}>",
        ]
        body = _raw_sdp(42000) if status == 200 and request.startswith("INVITE") else ""
        if body:
            lines.append("Content-Type: application/sdp")
        return ("\r\n".join([*lines, f"Content-Length: {len(body)}"]) + "\r\n\r\n" + body).encode()


class _RawCaller:
    """One hand-built UDP call towards the SBC, so a test can script the exact dialog a peer like CUCM sends."""

    def __init__(self, sbc_port: int) -> None:
        self.call_id = f"raw-{uuid.uuid4().hex[:10]}@127.0.0.1"
        self.relay_port = 0
        self._sbc = ("127.0.0.1", sbc_port)
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind(("127.0.0.1", 0))
        self._sock.settimeout(5)
        self._port = self._sock.getsockname()[1]
        self._tag = f"raw{self._port}"
        self._to_tag = ""
        self._cseq = 0
        self._branch = ""

    def close(self) -> None:
        self._sock.close()

    def _request(self, method: str, *, body: str = "", bump_cseq: bool = True, new_branch: bool = True) -> None:
        if bump_cseq:
            self._cseq += 1
        if new_branch:
            self._branch = f"z9hG4bK{uuid.uuid4().hex[:12]}"
        to_tag = f";tag={self._to_tag}" if self._to_tag else ""
        lines = [
            f"{method} sip:service@127.0.0.1:{self._sbc[1]} SIP/2.0",
            f"Via: SIP/2.0/UDP 127.0.0.1:{self._port};branch={self._branch}",
            "Max-Forwards: 70",
            f"From: <sip:raw@127.0.0.1:{self._port}>;tag={self._tag}",
            f"To: <sip:service@127.0.0.1:{self._sbc[1]}>{to_tag}",
            f"Call-ID: {self.call_id}",
            f"CSeq: {self._cseq} {method}",
            f"Contact: <sip:raw@127.0.0.1:{self._port}>",
        ]
        if body:
            lines.append("Content-Type: application/sdp")
        message = "\r\n".join([*lines, f"Content-Length: {len(body)}"]) + "\r\n\r\n" + body
        self._sock.sendto(message.encode(), self._sbc)

    def _final(self, method: str) -> str:
        while True:
            text = self._sock.recvfrom(65535)[0].decode(errors="replace")
            status = re.match(r"SIP/2\.0 (\d+)", text)
            if status and int(status.group(1)) >= 200 and re.search(rf"^CSeq: \d+ {method}", text, re.MULTILINE):
                tag = re.search(r"^To:.*;tag=([^;>\r\n]+)", text, re.MULTILINE)
                self._to_tag = tag.group(1) if tag else self._to_tag
                return text

    def invite(self, rtp_port: int) -> str:
        """Sends the INVITE and ACKs the final response; returns it."""
        self._request("INVITE", body=_raw_sdp(rtp_port))
        response = self._final("INVITE")
        if response.startswith("SIP/2.0 200"):
            self.relay_port = int(re.search(r"^m=audio (\d+)", response, re.MULTILINE).group(1))
            self._request("ACK", bump_cseq=False)
        else:
            self._request("ACK", bump_cseq=False, new_branch=False)
        return response

    def reinvite_offerless_answering(self, rtp_port: int) -> None:
        """An offerless re-INVITE whose ACK answers the SBC's offer with media at `rtp_port` on the SBC itself."""
        self._request("INVITE")
        assert self._final("INVITE").startswith("SIP/2.0 200")
        self._request("ACK", body=_raw_sdp(rtp_port), bump_cseq=False)

    def bye(self) -> None:
        try:
            self._request("BYE")
            self._final("BYE")
        except OSError:
            pass


def _wait_for_log(engine, needle: str, start: int, timeout_s: float = 5.0) -> str | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        for line in engine.log_lines[start:]:
            if needle in line:
                return line
        time.sleep(0.05)
    return None


def test_b2bua_transfer_chain_logged(sbc_engine, ports):
    """CUCM transfers by re-pointing each call at the other call's media, which the SBC anchors. Each call's
    offerless re-INVITE ACK then names the other session's relay port, and the engine must log it as a transfer."""
    if sbc_engine.process is None:
        pytest.skip("asserts on the log of the engine this suite spawned")
    start = len(sbc_engine.log_lines)
    uas = _RawUas(ports.callee_port)
    first, second = _RawCaller(ports.sbc_port), _RawCaller(ports.sbc_port)
    try:
        assert first.invite(41000).startswith("SIP/2.0 200")
        assert second.invite(41002).startswith("SIP/2.0 200")
        first.reinvite_offerless_answering(second.relay_port)
        second.reinvite_offerless_answering(first.relay_port)
        first_line = _wait_for_log(
            sbc_engine, f"[{first.call_id}] transfer: caller leg now relayed through call [{second.call_id}]", start
        )
        second_line = _wait_for_log(
            sbc_engine, f"[{second.call_id}] transfer: caller leg now relayed through call [{first.call_id}]", start
        )
    finally:
        first.bye()
        second.bye()
        first.close()
        second.close()
        uas.stop()

    assert first_line is not None, f"no transfer log for the first call:\n{sbc_engine.log_tail()}"
    assert second_line is not None, f"no transfer log for the second call:\n{sbc_engine.log_tail()}"
    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"


def test_b2bua_callee_rejection_logged(sbc_engine, ports):
    """A callee that rejects the INVITE is relayed to the caller, and the engine log says why setup failed."""
    if sbc_engine.process is None:
        pytest.skip("asserts on the log of the engine this suite spawned")
    start = len(sbc_engine.log_lines)
    uas = _RawUas(ports.callee_port, status=404)
    caller = _RawCaller(ports.sbc_port)
    try:
        response = caller.invite(41010)
        line = _wait_for_log(
            sbc_engine,
            f"[{caller.call_id}] setup failed: callee rejected the call with 404"
            f" (towards sip:service@127.0.0.1:{ports.callee_port})",
            start,
        )
    finally:
        caller.close()
        uas.stop()

    assert response.startswith("SIP/2.0 404"), f"caller was not told about the rejection:\n{response}"
    assert line is not None, f"no setup-failure log:\n{sbc_engine.log_tail()}"
    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
