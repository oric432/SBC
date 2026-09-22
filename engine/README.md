# engine

C++26 SBC.
High-level design and state-machine philosophy
are in the root [`AGENTS.md`](../AGENTS.md); this file covers the engine's own layout.

## Quick Start with Just

To see all available commands, simply run:
```bash
just
```
To quickly build and run in development mode:
```bash
just run-dev
```

## Configuration

Before running, copy `settings-example.toml` to `settings.toml` (repo root — the binary reads
`settings.toml` from its working directory) and fill in the placeholder `[IP_ADDRESS]` values.

| Section | Key | Default | Meaning |
| --- | --- | --- | --- |
| `[logging]` | `level` | `"info"` | App log verbosity (spdlog levels). |
| `[logging]` | `pjsip_level` | `"disabled"` | PJSIP's own log verbosity. `"disabled"` (or anything unrecognized) maps to `0`; otherwise it's parsed as a native PJSIP level `0`-`6` (see `pj_log_set_level`). |
| `[sip]` | `address` | `"127.0.0.1"` | IP the SIP transport binds to. Must be a real interface address, not `0.0.0.0`, if you want SDP-anchored RTP to reach this box from other hosts. |
| `[sip]` | `advertised_address` | `""` | Address advertised in rewritten SDP and Contact headers. Empty means "same as `address`"; set separately for NAT/reverse-proxy/multi-homed deployments. |
| `[sip]` | `port` | `5060` | Local SIP listening port. |
| `[sip]` | `identity_user` | `"sbc"` | User part of this SBC's own Contact/From URI. |
| `[sip]` | `invite_timeout_ms` | `32000` | How long to wait for a final response to an outbound INVITE before timing it out. |
| `[sip]` | `rtp_inactivity_timeout_s` | `60` | Ends an established call after this many seconds without RTP from either leg. `0` disables the check. |
| `[control_plane]` | `ws_url` | `"ws://127.0.0.1:3001/ws/engine"` | Websocket URL the engine connects to for its route table and (once it lands) SIP user snapshots. Plaintext `ws://` only -- TLS is out of scope for the whole engine today. |
| `[control_plane]` | `connect_timeout_s` | `5` | Seconds to wait for each connect/handshake attempt before treating it as failed. |
| `[control_plane]` | `retry_interval_s` | `5` | Seconds to wait between reconnect attempts. |
| `[registrar]` | `min_expires_s` | `60` | Below this, a REGISTER is rejected with `423 Interval Too Brief` instead of granted a shorter expiry. |
| `[registrar]` | `max_expires_s` | `120` | Upper bound on a granted registration's expiry; also the default when a REGISTER specifies none. |
| `[registrar]` | `binding_sweep_interval_s` | `60` | How often to drop registrations that expired without an explicit de-register. `0` disables sweeping. |

## Build & test

From the repo root:

```bash
# Configure and build (debug preset automatically enables clang-tidy)
cd engine
cmake --preset debug
cmake --build --preset build-debug
cd ..

# Run unit tests
ctest --test-dir engine/build

# Run the SBC engine
./engine/build/SbcEngine
```

## Code Quality (Formatting & Linting)

This project strictly enforces code formatting and static analysis:

1. **Formatting**: Run the CMake `format` target to automatically format all source files using `clang-format`.
   ```bash
   cmake --build engine/build --target format
   ```
2. **Linting (`clang-tidy`)**: Static analysis is deeply integrated into the CMake build. It is automatically enabled when you configure using the `debug` or `relwithdebinfo` presets (which set `ENABLE_CLANG_TIDY=true`). Any clang-tidy warnings will appear as standard compiler warnings/errors during `cmake --build engine/build`.

Unit tests cover the state machines (mocked actions), `Sdp`, `extract_utils`, `RoutesManager`,
`ReinviteHandler::media_changed` and the RTP/transcoding classes under `src/net/rtp/`. `PjsipStack`,
`MessageRouter` and the per-call adapters/handlers have no unit coverage — verify changes there with
`just test-b2bua` (live SIPp calls, see `tests/integration/b2bua/README.md`).
