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

RTP is streamed at realistic packet pacing (~30ms per packet) by both legs,
using `g711a.pcap` (G.711) unless a scenario's callee overrides it (see
`transcode_mismatched_codecs` below).

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

`transcode_mismatched_codecs` is an example of this: `callee_transcode.xml.j2`
statically answers G722 even though the caller only ever offers PCMU (valid —
the SBC's callee-facing offer always includes G722 too, see
`Sdp::restrict_audio_codecs()`), forcing the two legs onto different codecs
and exercising `MediaBridge`'s transcode path (issue #177) instead of pure
passthrough. There's no dedicated success log line for this path — a clean
call (no crash, no media-relay error in the engine's own logs) is what
`has_error_logs()` is actually confirming here.

That scenario plays `g722.pcap` rather than `g711a.pcap` — it's genuinely
G.722-encoded, not just relabeled, so a manual listen (e.g. Wireshark's RTP
Player on a capture of the call) on the transcoded output actually verifies
audio correctness rather than only "did the decode/resample/encode path run
without crashing." `g722.pcap` is `g711a.pcap`'s *exact* audio content (same
SSRC, sequence numbers, RTP timestamps, and packet sizes — regenerated from
it, not an independent recording), so the two are directly comparable
sample-for-sample; only the RTP payload type byte and the encoded payload
bytes differ. Regenerated with:

```bash
ffmpeg -f alaw -ar 8000 -ac 1 -i g711a_rtp_payload.alaw -ar 16000 -c:a g722 -f g722 g711a.g722
```

(`g711a_rtp_payload.alaw` is `g711a.pcap`'s 236 RTP payloads concatenated in
sequence order; both G.711 and G.722 run at the same 64kbit/s wire rate, so
the encoded output re-slices back into the original 236 packets' 240-byte
payloads 1:1, with the RTP/UDP/IP headers otherwise untouched apart from the
payload type byte and a recomputed UDP checksum.)
