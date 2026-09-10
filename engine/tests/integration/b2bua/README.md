# B2BUA integration tests

pytest + SIPp integration tests for the SBC's B2BUA call handling. See
`../README.md` for the shared engine-lifecycle fixtures this suite builds on.

## Running

```bash
just test-b2bua          # from engine/
```

This installs the Python dependencies declared in `../../pyproject.toml`,
builds nothing itself (build the engine first with `just build-dev`), and
runs pytest. It also checks that `sipp` is installed and has `CAP_NET_RAW`
(needed for `play_pcap_audio`) and skips with instructions if not:

```bash
sudo setcap cap_net_raw+ep "$(readlink -f "$(command -v sipp)")"
```

RTP (`g711a.pcap`) is streamed at realistic G.711 packet pacing (~30ms per
packet) by both legs.

## Adding a new scenario

Scenario XML lives in `templates/*.xml.j2` (Jinja2), rendered per test case by
the `render_scenario` fixture, and case data lives in `scenarios.py`. The
current cases (`bye_after_hold`, `sustained_rtp_relay`) are both expressed as
a single `holds_ms` list on the caller template — a new case that only needs
different timing doesn't need a new template, just a new `B2buaScenario`
entry.

A case that needs genuinely different SIP behavior (registration, a media
failure injected mid-call, etc.) should add a new template rather than
overloading the existing one, and should assert on engine-side behavior via
`sbc_engine.has_error_logs()` / `.log_lines` (see `../conftest.py`) rather
than relying solely on SIPp's own exit code, which only proves the scripted
message exchange completed — not that the engine actually behaved correctly
(e.g. tore down the `CallSession`).
