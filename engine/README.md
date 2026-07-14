# engine

C++26 B2BUA/SBC core. Target: `SbcEngine` (see `CMakeLists.txt`). Owns both SIP legs of every
call, rewrites SDP, and anchors RTP through itself. High-level design and state-machine philosophy
are in the root [`AGENTS.md`](../AGENTS.md); this file covers the engine's own layout.

## Layout

```
SbcEngine.cpp        entry point: loads settings, fetches route snapshot, boots PJSIP + asio, wires everything into MessageRouter
call/                CallSession (owns a call's 2 PJSIP inv_sessions, 2 RtpSessions, SM instances) and CallManager (Call-ID -> CallSession map)
router/              MessageRouter dispatches incoming SIP messages to the Setup/Dialog/Options state machines; *_actions.{hpp,cpp} are the SM action implementations
sm/                  Boost.SML state machines (setup_sm, dialog_sm, options_sm) plus shared events/types
sip/                 PjsipStack (RAII init/shutdown of the PJSIP endpoint) and sdp_mangler (SDP rewriting for RTP anchoring)
rtp/                 RtpSession (asio-based relay, symmetric-RTP latching) and the RTP/RTCP packet types (RtpPacket, PayloadTypes, endianness)
routes/              RoutesStore (thread-safe in-memory route table) and RoutesClient (fetches the snapshot from control-plane over HTTP via glaze)
protocols/           SipRoutes.hpp — the route rule/snapshot schema shared with control-plane (source of truth for schemas/b2bua/*.json, see tools/schema_generator.cpp)
config/              Settings loaded from settings.toml (glaze-reflected struct)
utils/               assert/error/log helpers, sdp_validator
```

## Call flow

`SbcEngine.cpp` loads `settings.toml`, fetches the routing snapshot from control-plane
(`routes/routes_client.cpp`) into a `RoutesStore`, initializes PJSIP (`sip/pjsip_init.cpp`), and
constructs a `MessageRouter` wired to a shared `SbcContext` (endpoint, io_context, config,
`CallManager`, `RoutesStore`). Inbound SIP messages reach `MessageRouter`, which maps PJSIP
inv-session state changes onto Setup/Dialog SM events (see root `AGENTS.md` for the transition
tables) and delegates side effects to `real_setup_actions` / `real_dialog_actions` /
`options_actions`. Each in-progress call is a `CallSession`, tracked by `CallManager` under its
Call-ID.

## Build & test

From the repo root:

```bash
cmake --preset debug && cmake --build --preset build-debug
ctest --test-dir build          # Catch2 SM tests (tests/test_setup_sm.cpp, test_dialog_sm.cpp, test_options_sm.cpp)
./build/sbc
```

`tests/` covers the state machines via mocked actions (`mock_sbc_actions.hpp`). Everything else in
this directory (RTP packet parsing, `sdp_mangler`, `SdpValidator`, `PjsipStack`, `MessageRouter`)
has no unit coverage yet — verify changes there against a live call (see root `AGENTS.md` for build
commands) until that gap is closed.

## Known gaps

- `SdpValidator::is_valid_offer/answer` only check for a `v=0` prefix — not a real SDP grammar check.
- `rtp/endianness.hpp` `read_big_endian<uint64_t>`/`write_big_endian<uint64_t>` have known bugs
  (missing read branch, wrong write shifts) — don't rely on 64-bit big-endian round-tripping there.
- `router/message_router.cpp` takes raw `pjsip_rx_data*` with no injectable seam, so it can't be
  unit-tested in isolation without a refactor (e.g. an `IRxDataReader` interface).
- Re-INVITE handling in the Dialog SM actions is stubbed.
