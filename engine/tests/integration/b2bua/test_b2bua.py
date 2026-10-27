from __future__ import annotations

import logging
import re
import socket
import struct
import threading
import time
import urllib.parse
import uuid
from dataclasses import dataclass
from pathlib import Path

import pytest

from .conftest import _PCAP_PATHS
from .scenarios import SCENARIOS

_log = logging.getLogger("integration.b2bua")

_HOLD_TIMEOUT_SETTINGS = {"sip": {"max_hold_duration_s": 5}, "logging": {"level": "trace"}}
# The termination event precedes removal; trace logs also prove cleanup and destruction.
_HOLD_TIMEOUT_LOG_MESSAGES = {
    "timeout": "ending call because the hold timeout expired",
    "cleanup": "dialog cleanup complete",
    "destroyed": "CallSession destroyed, releasing pool",
}


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


def _assert_hold_timeout_removed(sbc_engine, caller_xml: Path) -> None:
    messages = caller_xml.with_suffix(".messages.log").read_text()
    match = re.search(r"^Call-ID:\s*(\S+)", messages, re.MULTILINE)
    assert match is not None, messages
    call_id = match.group(1)
    expected = {stage: f"[{call_id}] {message}" for stage, message in _HOLD_TIMEOUT_LOG_MESSAGES.items()}
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        lines = sbc_engine.log_lines[sbc_engine.log_start :]
        if any(expected["destroyed"] in line for line in lines):
            break
        time.sleep(0.05)
    else:
        pytest.fail(f"session {call_id} was not removed and destroyed:\n{sbc_engine.log_tail()}")

    assert sum(expected["timeout"] in line for line in lines) == 1, sbc_engine.log_tail()
    assert any(expected["cleanup"] in line for line in lines), sbc_engine.log_tail()
    assert not sbc_engine.has_error_logs(), sbc_engine.log_tail()


@pytest.mark.parametrize("sbc_engine", [_HOLD_TIMEOUT_SETTINGS], indirect=True, ids=["5s-hold-limit"])
@pytest.mark.parametrize("direction", ["sendonly", "inactive"])
def test_b2bua_hold_timeout_terminates_and_removes_call(sbc_engine, render_scenario, run_sipp_pair, direction):
    caller_xml = render_scenario(
        "hold_timeout_caller.xml.j2",
        scenario_name="hold_timeout",
        media_steps=[{"direction": direction, "pause_ms": 0}],
    )
    callee_xml = render_scenario("callee.xml.j2", scenario_name="hold_timeout")

    run_sipp_pair(caller_xml, callee_xml, trace=True)
    _assert_hold_timeout_removed(sbc_engine, caller_xml)


@pytest.mark.parametrize("sbc_engine", [_HOLD_TIMEOUT_SETTINGS], indirect=True, ids=["5s-hold-limit"])
def test_b2bua_hold_timeout_resets_after_resume(sbc_engine, render_scenario, run_sipp_pair):
    # Each 3s hold leaves 2s headroom; the 6s resume crosses the old deadline and total hold exceeds 5s.
    caller_xml = render_scenario(
        "hold_timeout_caller.xml.j2",
        scenario_name="hold_timeout_resume",
        media_steps=[
            {"direction": "sendonly", "pause_ms": 3000},
            {"direction": "sendrecv", "pause_ms": 6000},
            {"direction": "sendonly", "pause_ms": 3000},
            {"direction": "sendonly", "pause_ms": 0},
        ],
    )
    callee_xml = render_scenario("callee.xml.j2", scenario_name="hold_timeout_resume")

    # Successful re-INVITEs prove the same dialog survived the old deadline and cumulative hold limit.
    run_sipp_pair(caller_xml, callee_xml, trace=True)
    _assert_hold_timeout_removed(sbc_engine, caller_xml)


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


def _header_value(message: str, name: str) -> str:
    match = re.search(rf"^{re.escape(name)}:\s*(.*?)\s*$", message, re.MULTILINE | re.IGNORECASE)
    assert match is not None, f"missing {name} header:\n{message}"
    return match.group(1)


def _sip_method(message: str) -> str | None:
    match = re.match(r"([A-Z]+)\s+\S+\s+SIP/2\.0", message)
    return match.group(1) if match else None


def _sip_status(message: str) -> int | None:
    match = re.match(r"SIP/2\.0\s+(\d{3})", message)
    return int(match.group(1)) if match else None


def _sip_cseq(message: str) -> tuple[int, str]:
    match = re.search(r"^CSeq:\s*(\d+)\s+([A-Z]+)", message, re.MULTILINE | re.IGNORECASE)
    assert match is not None, f"missing CSeq header:\n{message}"
    return int(match.group(1)), match.group(2).upper()


def _sip_tag(message: str, header: str) -> str:
    match = re.search(r";tag=([^;>\s]+)", _header_value(message, header), re.IGNORECASE)
    assert match is not None, f"missing {header} tag:\n{message}"
    return match.group(1)


@dataclass
class _SipDatagram:
    text: str
    source: tuple[str, int]


@dataclass
class _InboundDialog:
    call_id: str
    from_tag: str
    invite: str
    provisional: str | None = None
    final: str | None = None

    @property
    def to_tag(self) -> str:
        assert self.provisional is not None or self.final is not None
        return _sip_tag(self.final or self.provisional or "", "To")


class _RawSipPeer:
    """Small message-driven SIP endpoint used when one test needs several concurrent dialogs."""

    def __init__(self, sbc_port: int, local_port: int = 0) -> None:
        self._sbc = ("127.0.0.1", sbc_port)
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind(("127.0.0.1", local_port))
        self._port = self._sock.getsockname()[1]
        self._pending: list[_SipDatagram] = []

    def close(self) -> None:
        self._sock.close()

    def _receive(self, predicate, timeout_s: float = 5.0) -> _SipDatagram:
        for index, datagram in enumerate(self._pending):
            if predicate(datagram.text):
                return self._pending.pop(index)

        deadline = time.monotonic() + timeout_s
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                pytest.fail("timed out waiting for a matching SIP message")
            self._sock.settimeout(remaining)
            try:
                data, source = self._sock.recvfrom(65535)
            except TimeoutError:
                pytest.fail("timed out waiting for a matching SIP message")
            datagram = _SipDatagram(data.decode(errors="replace"), source)
            if predicate(datagram.text):
                return datagram
            self._pending.append(datagram)

    def request(self, method: str, call_id: str | None = None, timeout_s: float = 5.0) -> _SipDatagram:
        return self._receive(
            lambda message: _sip_method(message) == method
            and (call_id is None or _header_value(message, "Call-ID") == call_id),
            timeout_s,
        )

    def response(
        self,
        cseq_method: str,
        call_id: str,
        status: int | None = None,
        timeout_s: float = 5.0,
    ) -> _SipDatagram:
        return self._receive(
            lambda message: _sip_status(message) is not None
            and _sip_cseq(message)[1] == cseq_method
            and _header_value(message, "Call-ID") == call_id
            and (status is None or _sip_status(message) == status),
            timeout_s,
        )

    def assert_no_request(self, method: str, call_id: str | None = None, timeout_s: float = 0.25) -> None:
        for datagram in self._pending:
            if _sip_method(datagram.text) == method and (
                call_id is None or _header_value(datagram.text, "Call-ID") == call_id
            ):
                pytest.fail(f"unexpected {method}:\n{datagram.text}")

        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            self._sock.settimeout(deadline - time.monotonic())
            try:
                data, source = self._sock.recvfrom(65535)
            except TimeoutError:
                return
            datagram = _SipDatagram(data.decode(errors="replace"), source)
            if _sip_method(datagram.text) == method and (
                call_id is None or _header_value(datagram.text, "Call-ID") == call_id
            ):
                pytest.fail(f"unexpected {method}:\n{datagram.text}")
            self._pending.append(datagram)

    def invite(self, *, replaces: str | None = None, early_only: bool = False) -> _InboundDialog:
        call_id = f"raw-{uuid.uuid4().hex}@127.0.0.1"
        from_tag = f"peer-{uuid.uuid4().hex[:12]}"
        branch = f"z9hG4bK-{uuid.uuid4().hex[:12]}"
        extra_headers = []
        if replaces is not None:
            suffix = ";early-only" if early_only else ""
            extra_headers.extend(["Require: replaces", f"Replaces: {replaces}{suffix}"])
        invite = self._build_request(
            "INVITE",
            call_id,
            from_tag,
            "",
            1,
            branch,
            extra_headers,
            _raw_sdp(41000),
        )
        self._sock.sendto(invite.encode(), self._sbc)
        return _InboundDialog(call_id, from_tag, invite)

    def cancel(self, dialog: _InboundDialog) -> None:
        cancel = re.sub(r"^INVITE ", "CANCEL ", dialog.invite, count=1)
        cancel = re.sub(r"^CSeq:\s*(\d+)\s+INVITE", r"CSeq: \1 CANCEL", cancel, flags=re.MULTILINE)
        cancel = re.sub(r"Content-Type: application/sdp\r\n", "", cancel)
        cancel = re.sub(r"Content-Length: \d+\r\n\r\n.*", "Content-Length: 0\r\n\r\n", cancel, flags=re.DOTALL)
        self._sock.sendto(cancel.encode(), self._sbc)

    def ack(self, dialog: _InboundDialog) -> None:
        assert dialog.final is not None
        status = _sip_status(dialog.final)
        branch = (
            re.search(r"branch=([^;\s]+)", _header_value(dialog.invite, "Via"), re.IGNORECASE).group(1)
            if status is not None and status >= 300
            else f"z9hG4bK-{uuid.uuid4().hex[:12]}"
        )
        ack = self._build_request(
            "ACK",
            dialog.call_id,
            dialog.from_tag,
            dialog.to_tag,
            1,
            branch,
            [],
            "",
        )
        self._sock.sendto(ack.encode(), self._sbc)

    def bye(self, dialog: _InboundDialog, cseq: int = 2) -> None:
        bye = self._build_request(
            "BYE",
            dialog.call_id,
            dialog.from_tag,
            dialog.to_tag,
            cseq,
            f"z9hG4bK-{uuid.uuid4().hex[:12]}",
            [],
            "",
        )
        self._sock.sendto(bye.encode(), self._sbc)

    def reply(self, request: _SipDatagram, status: int, *, to_tag: str | None = None, body: str = "") -> str:
        reasons = {
            180: "Ringing",
            200: "OK",
            481: "Call/Transaction Does Not Exist",
            486: "Busy Here",
            487: "Request Terminated",
        }
        to_line = _header_line(request.text, "To")
        if to_tag is not None and ";tag=" not in to_line:
            to_line += f";tag={to_tag}"
        lines = [
            f"SIP/2.0 {status} {reasons[status]}",
            *(via.rstrip() for via in re.findall(r"^Via:.*$", request.text, re.MULTILINE | re.IGNORECASE)),
            _header_line(request.text, "From"),
            to_line,
            _header_line(request.text, "Call-ID"),
            _header_line(request.text, "CSeq"),
            f"Contact: <sip:peer@127.0.0.1:{self._port}>",
        ]
        if body:
            lines.append("Content-Type: application/sdp")
        response = "\r\n".join([*lines, f"Content-Length: {len(body)}"]) + "\r\n\r\n" + body
        self._sock.sendto(response.encode(), request.source)
        return response

    def _build_request(
        self,
        method: str,
        call_id: str,
        from_tag: str,
        to_tag: str,
        cseq: int,
        branch: str,
        extra_headers: list[str],
        body: str,
    ) -> str:
        tagged_to = f";tag={to_tag}" if to_tag else ""
        lines = [
            f"{method} sip:service@127.0.0.1:{self._sbc[1]} SIP/2.0",
            f"Via: SIP/2.0/UDP 127.0.0.1:{self._port};branch={branch}",
            "Max-Forwards: 70",
            f"From: <sip:peer@127.0.0.1:{self._port}>;tag={from_tag}",
            f"To: <sip:service@127.0.0.1:{self._sbc[1]}>{tagged_to}",
            f"Call-ID: {call_id}",
            f"CSeq: {cseq} {method}",
            f"Contact: <sip:peer@127.0.0.1:{self._port}>",
            *extra_headers,
        ]
        if body:
            lines.append("Content-Type: application/sdp")
        return "\r\n".join([*lines, f"Content-Length: {len(body)}"]) + "\r\n\r\n" + body


@dataclass
class _B2buaCall:
    incoming: _InboundDialog
    outgoing_invite: _SipDatagram
    outgoing_progress: str

    @property
    def outgoing_call_id(self) -> str:
        return _header_value(self.outgoing_invite.text, "Call-ID")

    @property
    def caller_facing_replaces(self) -> str:
        return f"{self.incoming.call_id};to-tag={self.incoming.to_tag};from-tag={self.incoming.from_tag}"

    @property
    def callee_facing_replaces(self) -> str:
        return (
            f"{self.outgoing_call_id};to-tag={_sip_tag(self.outgoing_progress, 'To')};"
            f"from-tag={_sip_tag(self.outgoing_invite.text, 'From')}"
        )


def _start_raw_call(caller: _RawSipPeer, callee: _RawSipPeer, *, confirm: bool) -> _B2buaCall:
    incoming = caller.invite()
    outgoing = callee.request("INVITE")
    assert _header_value(outgoing.text, "Call-ID") != incoming.call_id
    assert _sip_cseq(outgoing.text)[1] == "INVITE"

    outgoing_tag = f"callee-{uuid.uuid4().hex[:10]}"
    outgoing_progress = callee.reply(outgoing, 180, to_tag=outgoing_tag)
    incoming.provisional = caller.response("INVITE", incoming.call_id, 180).text
    assert _sip_tag(incoming.provisional, "From") == incoming.from_tag

    call = _B2buaCall(incoming, outgoing, outgoing_progress)
    if confirm:
        callee.reply(outgoing, 200, to_tag=outgoing_tag, body=_raw_sdp(42000))
        outgoing_ack = callee.request("ACK", call.outgoing_call_id)
        assert _sip_cseq(outgoing_ack.text) == (_sip_cseq(outgoing.text)[0], "ACK")
        incoming.final = caller.response("INVITE", incoming.call_id, 200).text
        caller.ack(incoming)
    return call


def _accept_replacement(
    caller: _RawSipPeer,
    callee: _RawSipPeer,
    replacement: _InboundDialog,
    expected_replaces: str,
) -> _SipDatagram:
    outbound = callee.request("INVITE")
    assert _header_value(outbound.text, "Call-ID") != replacement.call_id
    assert _header_value(outbound.text, "Replaces") == expected_replaces
    assert "replaces" in _header_value(outbound.text, "Require").lower()
    callee.reply(outbound, 200, to_tag=f"replacement-{uuid.uuid4().hex[:10]}", body=_raw_sdp(42002))
    ack = callee.request("ACK", _header_value(outbound.text, "Call-ID"))
    assert _sip_cseq(ack.text) == (_sip_cseq(outbound.text)[0], "ACK")
    replacement.final = caller.response("INVITE", replacement.call_id, 200).text
    caller.ack(replacement)
    return outbound


def _accept_local_early_replacement(
    caller: _RawSipPeer,
    callee: _RawSipPeer,
    replacement: _InboundDialog,
) -> _SipDatagram:
    outbound = callee.request("INVITE")
    assert _header_value(outbound.text, "Call-ID") != replacement.call_id
    assert not re.search(r"^Replaces:", outbound.text, re.MULTILINE | re.IGNORECASE)
    assert not re.search(r"^Require:.*\breplaces\b", outbound.text, re.MULTILINE | re.IGNORECASE)
    callee.reply(outbound, 200, to_tag=f"replacement-{uuid.uuid4().hex[:10]}", body=_raw_sdp(42002))
    ack = callee.request("ACK", _header_value(outbound.text, "Call-ID"))
    assert _sip_cseq(ack.text) == (_sip_cseq(outbound.text)[0], "ACK")
    replacement.final = caller.response("INVITE", replacement.call_id, 200).text
    caller.ack(replacement)
    return outbound


def _end_call_from_caller(
    caller: _RawSipPeer,
    callee: _RawSipPeer,
    incoming: _InboundDialog,
    outgoing_call_id: str,
) -> None:
    caller.bye(incoming)
    outbound_bye = callee.request("BYE", outgoing_call_id)
    assert _sip_cseq(outbound_bye.text)[1] == "BYE"
    callee.reply(outbound_bye, 200)
    response = caller.response("BYE", incoming.call_id, 200)
    assert _sip_cseq(response.text)[1] == "BYE"


def _answer_replacement_byes(
    caller: _RawSipPeer,
    callee: _RawSipPeer,
    old_call: _B2buaCall,
) -> None:
    caller_bye = caller.request("BYE", old_call.incoming.call_id)
    callee_bye = callee.request("BYE", old_call.outgoing_call_id)
    assert _sip_cseq(caller_bye.text)[1] == "BYE"
    assert _sip_cseq(callee_bye.text)[1] == "BYE"
    caller.reply(caller_bye, 200)
    callee.reply(callee_bye, 200)
    caller.assert_no_request("BYE", old_call.incoming.call_id)
    callee.assert_no_request("BYE", old_call.outgoing_call_id)


def test_b2bua_invite_replaces_confirmed_dialog_terminates_old_call_with_bye(sbc_engine, ports):
    caller = _RawSipPeer(ports.sbc_port)
    callee = _RawSipPeer(ports.sbc_port, ports.callee_port)
    try:
        old_call = _start_raw_call(caller, callee, confirm=True)
        replacement = caller.invite(replaces=old_call.caller_facing_replaces)
        replacement_outbound = _accept_replacement(
            caller,
            callee,
            replacement,
            old_call.callee_facing_replaces,
        )

        _answer_replacement_byes(caller, callee, old_call)
        _end_call_from_caller(
            caller,
            callee,
            replacement,
            _header_value(replacement_outbound.text, "Call-ID"),
        )
    finally:
        caller.close()
        callee.close()

    assert not sbc_engine.has_error_logs(), sbc_engine.log_tail()


def test_b2bua_invite_replaces_early_uac_dialog_cancels_old_invite(sbc_engine, ports):
    caller = _RawSipPeer(ports.sbc_port)
    callee = _RawSipPeer(ports.sbc_port, ports.callee_port)
    try:
        old_call = _start_raw_call(caller, callee, confirm=False)
        replacement = caller.invite(replaces=old_call.callee_facing_replaces, early_only=True)
        replacement_outbound = _accept_local_early_replacement(caller, callee, replacement)

        cancel = callee.request("CANCEL", old_call.outgoing_call_id)
        assert _sip_cseq(cancel.text) == (_sip_cseq(old_call.outgoing_invite.text)[0], "CANCEL")
        callee.reply(cancel, 200)
        callee.reply(
            old_call.outgoing_invite,
            487,
            to_tag=_sip_tag(old_call.outgoing_progress, "To"),
        )
        old_ack = callee.request("ACK", old_call.outgoing_call_id)
        assert _sip_cseq(old_ack.text) == (_sip_cseq(old_call.outgoing_invite.text)[0], "ACK")
        old_call.incoming.final = caller.response("INVITE", old_call.incoming.call_id, 487).text
        caller.ack(old_call.incoming)
        callee.assert_no_request("CANCEL", old_call.outgoing_call_id)

        _end_call_from_caller(
            caller,
            callee,
            replacement,
            _header_value(replacement_outbound.text, "Call-ID"),
        )
    finally:
        caller.close()
        callee.close()

    assert not sbc_engine.has_error_logs(), sbc_engine.log_tail()


def test_b2bua_invite_replaces_early_uas_dialog_is_rejected_and_original_completes(sbc_engine, ports):
    caller = _RawSipPeer(ports.sbc_port)
    callee = _RawSipPeer(ports.sbc_port, ports.callee_port)
    try:
        old_call = _start_raw_call(caller, callee, confirm=False)
        replacement = caller.invite(replaces=old_call.caller_facing_replaces)
        replacement.final = caller.response("INVITE", replacement.call_id, 481).text
        caller.ack(replacement)
        callee.assert_no_request("INVITE")
        callee.assert_no_request("CANCEL", old_call.outgoing_call_id)

        caller.cancel(old_call.incoming)
        cancel_response = caller.response("CANCEL", old_call.incoming.call_id, 200)
        assert _sip_cseq(cancel_response.text)[1] == "CANCEL"
        outbound_cancel = callee.request("CANCEL", old_call.outgoing_call_id)
        callee.reply(outbound_cancel, 200)
        callee.reply(
            old_call.outgoing_invite,
            487,
            to_tag=_sip_tag(old_call.outgoing_progress, "To"),
        )
        callee.request("ACK", old_call.outgoing_call_id)
        old_call.incoming.final = caller.response("INVITE", old_call.incoming.call_id, 487).text
        caller.ack(old_call.incoming)
    finally:
        caller.close()
        callee.close()

    assert not sbc_engine.has_error_logs(), sbc_engine.log_tail()


@pytest.mark.parametrize("rejection_status", [486, 481], ids=["busy", "dialog-does-not-exist"])
def test_b2bua_rejected_invite_replaces_preserves_confirmed_old_call(sbc_engine, ports, rejection_status):
    caller = _RawSipPeer(ports.sbc_port)
    callee = _RawSipPeer(ports.sbc_port, ports.callee_port)
    try:
        old_call = _start_raw_call(caller, callee, confirm=True)
        replacement = caller.invite(replaces=old_call.caller_facing_replaces)
        outbound = callee.request("INVITE")
        assert _header_value(outbound.text, "Replaces") == old_call.callee_facing_replaces
        callee.reply(outbound, rejection_status, to_tag=f"reject-{uuid.uuid4().hex[:10]}")
        callee.request("ACK", _header_value(outbound.text, "Call-ID"))
        replacement.final = caller.response("INVITE", replacement.call_id, rejection_status).text
        caller.ack(replacement)

        caller.assert_no_request("BYE", old_call.incoming.call_id)
        callee.assert_no_request("BYE", old_call.outgoing_call_id)
        caller.assert_no_request("CANCEL", old_call.incoming.call_id)
        callee.assert_no_request("CANCEL", old_call.outgoing_call_id)
        _end_call_from_caller(caller, callee, old_call.incoming, old_call.outgoing_call_id)
    finally:
        caller.close()
        callee.close()

    assert not sbc_engine.has_error_logs(), sbc_engine.log_tail()


def test_b2bua_invite_replaces_early_only_rejects_confirmed_dialog(sbc_engine, ports):
    caller = _RawSipPeer(ports.sbc_port)
    callee = _RawSipPeer(ports.sbc_port, ports.callee_port)
    try:
        old_call = _start_raw_call(caller, callee, confirm=True)
        replacement = caller.invite(replaces=old_call.caller_facing_replaces, early_only=True)
        replacement.final = caller.response("INVITE", replacement.call_id, 486).text
        assert ";early-only" in _header_value(replacement.invite, "Replaces")
        caller.ack(replacement)

        callee.assert_no_request("INVITE")
        caller.assert_no_request("BYE", old_call.incoming.call_id)
        callee.assert_no_request("BYE", old_call.outgoing_call_id)
        callee.assert_no_request("CANCEL", old_call.outgoing_call_id)
        _end_call_from_caller(caller, callee, old_call.incoming, old_call.outgoing_call_id)
    finally:
        caller.close()
        callee.close()

    assert not sbc_engine.has_error_logs(), sbc_engine.log_tail()


def test_b2bua_invite_replaces_cancel_200_race_acks_then_byes_old_dialog(sbc_engine, ports):
    caller = _RawSipPeer(ports.sbc_port)
    callee = _RawSipPeer(ports.sbc_port, ports.callee_port)
    try:
        old_call = _start_raw_call(caller, callee, confirm=False)
        replacement = caller.invite(replaces=old_call.callee_facing_replaces, early_only=True)
        replacement_outbound = _accept_local_early_replacement(caller, callee, replacement)

        cancel = callee.request("CANCEL", old_call.outgoing_call_id)
        assert _sip_cseq(cancel.text)[1] == "CANCEL"
        callee.reply(
            old_call.outgoing_invite,
            200,
            to_tag=_sip_tag(old_call.outgoing_progress, "To"),
            body=_raw_sdp(42000),
        )
        old_ack = callee.request("ACK", old_call.outgoing_call_id)
        old_bye = callee.request("BYE", old_call.outgoing_call_id)
        assert _sip_cseq(old_ack.text)[1] == "ACK"
        assert _sip_cseq(old_bye.text)[1] == "BYE"
        callee.reply(cancel, 200)
        callee.reply(old_bye, 200)

        old_call.incoming.final = caller.response("INVITE", old_call.incoming.call_id, 487).text
        caller.ack(old_call.incoming)
        callee.assert_no_request("BYE", old_call.outgoing_call_id)
        callee.assert_no_request("CANCEL", old_call.outgoing_call_id)
        _end_call_from_caller(
            caller,
            callee,
            replacement,
            _header_value(replacement_outbound.text, "Call-ID"),
        )
    finally:
        caller.close()
        callee.close()

    assert not sbc_engine.has_error_logs(), sbc_engine.log_tail()


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
