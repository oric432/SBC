from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class B2buaScenario:
    name: str
    # Milliseconds to hold after each pcap (re)trigger before the next one
    # (or, for the last entry, before sending BYE). One entry reproduces the
    # old "hold ~10s then hang up" case; several reproduce the old "loop
    # indefinitely" case but bounded, so it terminates on its own in CI.
    holds_ms: list[int]
    caller_template: str = "caller.xml.j2"
    callee_template: str = "callee.xml.j2"
    # Which fixture pcap each leg plays (see conftest.py's _PCAP_PATHS).
    caller_pcap: str = "g711a"
    callee_pcap: str = "g711a"


SCENARIOS = [
    B2buaScenario(name="bye_after_hold", holds_ms=[10_000]),
    B2buaScenario(name="sustained_rtp_relay", holds_ms=[7_080, 7_080, 7_080]),
    B2buaScenario(
        name="transcode_mismatched_codecs",
        holds_ms=[10_000],
        callee_template="callee_transcode.xml.j2",
        callee_pcap="g722",
    ),
    B2buaScenario(
        name="dtmf_pt_rewrite",
        holds_ms=[2_000],
        caller_template="caller_dtmf.xml.j2",
        callee_template="callee_dtmf.xml.j2",
        caller_pcap="dtmf",
    ),
]
