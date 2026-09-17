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

`dtmf_pt_rewrite` exercises `MediaBridge`'s DTMF-PT-rewrite path (issue #177)
the same way: `caller_dtmf.xml.j2` offers `telephone-event` under PT 101,
`callee_dtmf.xml.j2` answers it under PT 100, and the caller plays
`dtmf.pcap` — PCMU audio with one RFC 4733 digit event under PT 101 — so a
correct engine must relay those event packets to the callee with the header
byte rewritten to 100 (payload and timestamp untouched). As with the
transcode scenario, `has_error_logs()` only confirms the call completed
cleanly; the actual rewrite is best confirmed by capturing the callee's
media port during a run (`tcpdump -i lo udp port 6006`) and checking the
relayed event packets carry PT 100, not 101.

`dtmf.pcap` is synthesized (not a real recording): PCMU silence, one RFC 4733
digit event under PT 101 (marker on the first packet, three duration-update
packets, three redundant end-marked packets at the final duration — all
sharing the event's start timestamp, per RFC 4733 §3.6 — then PCMU resumes),
built with `scapy` in a throwaway venv rather than a tool checked into the
suite's own dependencies (see `../../pyproject.toml`).

`early_media` (issue #214) exercises the SBC relaying the callee's early SDP
answer to the caller as a 183, with real RTP flowing before the 200 OK. As
with the transcode and DTMF scenarios, `has_error_logs()` only confirms the
protocol exchange completed cleanly, not that the relayed audio itself is
correct — verify that with a capture:

```bash
sudo setcap cap_net_raw+ep "$(readlink -f "$(command -v tcpdump)")"  # once
tcpdump -i lo -n udp -w capture.pcap &
just test-b2bua -k test_b2bua_early_media
kill %1  # stop tcpdump once the run above finishes

# RTP relayed to the caller (port 6002), in sequence order:
tshark -r capture.pcap -d udp.port==6002,rtp -d udp.port==6006,rtp \
  -Y "rtp && udp.dstport==6002" -T fields -e rtp.seq -e rtp.payload \
  | sort -n | cut -f2 | tr -d '\n' | xxd -r -p > relayed.alaw
ffmpeg -f alaw -ar 8000 -ac 1 -i relayed.alaw relayed.wav
```

`relayed.wav` should be byte-identical, per packet, to `g711a.pcap`'s own
RTP payloads at the matching sequence numbers — this is a plain passthrough
(same codec on both legs, no transcode session), so nothing should differ.
`callee_early_media.xml.j2`'s pre-200-OK pause is only 500ms, which lands
entirely in `g711a.pcap`'s silent lead-in (`0xD5`, A-law's digital-silence
byte) — lengthen it locally (e.g. to 3000ms) to capture the fixture's
actual varying content instead.

`early_media_demo.wav` in this directory is exactly that: the pre-answer
segment relayed to the caller with the pause temporarily stretched to 3s,
confirmed byte-exact against `g711a.pcap` for all 168 packets relayed
during that run, including every packet sent before the 200 OK.

`update_early_media`, `update_early_media_prack` and `update_early_media_callee`
(issue #211) cover an offer-bearing UPDATE sent during early media, before the
call is Established -- on an unreliable caller leg, a reliable one, and on the
callee leg. Assertions are purely SDP-body `ereg`s (the new codec on the
UPDATE's own 200 OK, and either the INVITE's final 200 OK carrying that same
codec, or staying bodiless per RFC 3262 S5, depending on the leg); the
underlying transcode path is the same `MediaBridge::configure_legs()` already
covered above, so no separate audio capture is needed here.
