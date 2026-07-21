# engine

C++26 B2BUA/SBC core. Target: `SbcEngine` (see `CMakeLists.txt`). Owns both SIP legs of every
call, rewrites SDP, and anchors RTP through itself. High-level design and state-machine philosophy
are in the root [`AGENTS.md`](../AGENTS.md); this file covers the engine's own layout.

## Configuration

Before running, copy `settings-example.toml` to `settings.toml` (repo root — the binary reads
`settings.toml` from its working directory) and fill in the placeholder `[IP_ADDRESS]` values.
Startup aborts with `Log::crash_error` if the file is missing or fails to parse.

| Key | Default | Meaning |
| --- | --- | --- |
| `local_sip_address` | `"[IP_ADDRESS]"` | IP the SIP transport binds to. Must be a real interface address, not `0.0.0.0`, if you want SDP-anchored RTP to reach this box from other hosts. |
| `local_sip_port` | `5060` | Local SIP listening port. |
| `sip_identity_user` | `"sbc"` | User part of this SBC's own Contact/From URI. |
| `log_level` | `"info"` | App log verbosity (spdlog levels). |
| `pjsip_log_level` | `"disabled"` | PJSIP's own log verbosity. `"disabled"` (or anything unrecognized) maps to `0`; otherwise it's parsed as a native PJSIP level `0`-`6` (see `pj_log_set_level`). |
| `control_plane_address` | `"[IP_ADDRESS]"` | Host of the control-plane backend the engine fetches the SIP route table from at startup. |
| `control_plane_http_port` | `3001` | control-plane backend's HTTP port. |
| `connection_timeout` | `5` | Seconds to wait when fetching the route snapshot from control-plane. |

## Build & test

From the repo root:

```bash
cmake --preset debug && cmake --build --preset build-debug
ctest --test-dir build
./build/sbc
```

`tests/` covers the state machines via mocked actions. Everything else in
this directory (RTP packet parsing, `sdp_mangler`, `SdpValidator`, `PjsipStack`, `MessageRouter`)
has no unit coverage yet — verify changes there against a live call (see root `AGENTS.md` for build
commands) until that gap is closed.
