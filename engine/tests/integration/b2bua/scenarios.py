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


SCENARIOS = [
    B2buaScenario(name="bye_after_hold", holds_ms=[10_000]),
    B2buaScenario(name="sustained_rtp_relay", holds_ms=[7_080, 7_080, 7_080]),
]
