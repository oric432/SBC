# AGENTS.md

Guidance for coding agents (any LLM tool) working in this repo.

## What this is

SBC (Session Border Controller) project: a SIP B2BUA plus a web control plane for managing its
routing table. Three components:

| Component | Path | What it does |
| --- | --- | --- |
| Engine | `engine/` | C++26 B2BUA core (PJSIP + Boost.SML). Owns both SIP legs of every call, rewrites SDP, anchors RTP. See `engine/README.md` and `engine/AGENTS.md`. |
| Control-plane backend | `control-plane/backend/` | Express/TypeScript API, owns the SIP route table in Postgres. See its own `AGENTS.md`. |
| Control-plane frontend | `control-plane/frontend/` | React/Vite SPA for managing routes through the backend. See its own `AGENTS.md`. |

## Known Limitations

SIP features not currently supported by the engine: PRACK (100rel) orchestration, app-level
UPDATE, REFER, hold (a re-INVITE without an active audio line is rejected with 488), SIP forking,
ICE, SRTP, WebRTC.

## Commit conventions

Commit subject lines follow [Conventional Commits](https://www.conventionalcommits.org/):
`<type>[(scope)]: <subject>`, where `<type>` is one of `feat fix chore docs style refactor test
build ci perf`. Scope is freeform and optional (e.g. `feat(engine): ...`) — this is a monorepo
where a single PR can span components, so scope isn't validated against a fixed enum.

`just setup-all` configures `core.hooksPath` to `.githooks`, which blocks non-conforming commits
locally via `.githooks/commit-msg`. CI also checks every commit in a PR
(`scripts/check-commit-message.sh`) but only warns (a `::warning::` annotation) — the convention is
new, so it never blocks a PR, and pre-existing history isn't and won't be rewritten to conform.

## Engine architecture

This section covers the parts of the engine's design that aren't obvious from file names alone —
read it before touching `src/sip/` or `src/net/rtp/`. Build/style conventions live in
`engine/AGENTS.md`; this is architecture and invariants only.

### Layout (`engine/src/sip/`)

- `stack/` — PJSIP bring-up (`PjsipStack`) and stateless helpers over the C API: `Sdp` (parse,
  extract, rewrite) and `Inv` (the invite-session send/answer/end wrappers every adapter uses).
- `router/` — `MessageRouter`, the single entry point for PJSIP callbacks: dispatches
  out-of-dialog requests by method, and routes invite-session callbacks to the owning
  `CallSession`'s setup or dialog adapter depending on the setup machine's state.
- `sm/` — the Boost.SML machines (`setup_sm.hpp`, `dialog_sm.hpp`, `offer_answer_sm.hpp`,
  `options_sm.hpp`), their events, the `I*Actions` interface each machine drives, and `SmRunner`,
  the one pimpl owning a machine plus its logger (`*_sm_runner.hpp` are thin subclasses adding
  named state queries). Nothing in `sm/` touches PJSIP.
- `call/` — one call's state and its PJSIP adapters: `CallSession` (both legs, the `MediaBridge`,
  the two machines and their adapters), `SetupActions`, `OfferAnswerExchange` + `OfferAnswerActions`
  (the initial INVITE's offer/answer relay), and `DialogActions`, a facade over one handler per
  in-dialog method in `call/handlers/` (`ByeHandler`, `ReinviteHandler`). A new in-dialog method
  (UPDATE, REFER) is a new handler plus a line in the facade.
- `route_table/` — the route snapshot fetched from the control plane.

### Threading model

- One thread runs the PJSIP poll loop (`PjsipStack::run()` calling `pjsip_endpt_handle_events()`
  in a loop). Every SM transition and `CallManager` mutation happens serialized on this thread.
  There are no PJSIP worker threads.
- A second thread runs a Boost.Asio `io_context`, used **only** for the RTP relay
  (`MediaBridge`).
- Cross-thread shared state is narrow and already protected — don't assume the rest of the
  codebase needs locking, and don't add new cross-thread shared mutable state without equivalent
  protection:
  - `MediaBridge::last_packet_time()` — written by the RTP thread, safely read by the SIP thread
    (see the comment in `media_bridge.hpp`).
  - `MediaBridge::set_error_handler()` is explicitly **not** thread-safe against a running relay
    loop — it must be called before `start_bridge_loop()`, never after.
  - `MediaBridge::configure_legs()` opens the codec session synchronously on the SIP thread and
    posts the swap to the RTP thread, so it is safe to call on a running relay loop.
  - `RoutesStore`'s route-snapshot swap is mutex-guarded.
  - `RtpInactivityTimer`'s scan-pending flag is atomic.

### State machines (`src/sip/sm/`)

Four Boost.SML machines, each templated on the `I*Actions` interface it drives:

- **`SetupSm`** — call setup: `Idle → Routing → Negotiating → Ringing → Established`, with
  `Cancelling` and `Failed → Done` off-ramps. Routing resolution and the exchange outcome arrive
  as events; the exchange itself is `OfferAnswerSm`.
- **`OfferAnswerSm`** — one initial-INVITE offer/answer relay, owned by `OfferAnswerExchange` for
  the duration of setup only: `Idle → AwaitingAnswer → RelayingAnswer → AwaitingAck → Committed`,
  or `RolledBack` / `Failed`, then `Done`. It publishes exactly one `ExchangeOutcome` to setup.
- **`DialogSm`** — the confirmed-dialog phase: `Active → Reinviting → Active` for re-INVITEs
  (answered locally on the offering leg, never forwarded to the other leg; a colliding re-INVITE
  gets 491) and `Active → Terminating → Terminated → DialogDone` for BYE and errors. There is no
  offer/answer exchange in this phase.
- **`OptionsSm`** — stateless OPTIONS responder; `MessageRouter` keeps one runner and resets it per
  request.

**Self-fire pattern (all machines)**: when an action can deterministically know the next event
before external code does (e.g. a routing lookup's outcome), it does **not** re-entrantly call
`process_event()`. Instead it takes a `Sml::back::process<...>` queue parameter by value and calls
that; boost::sml drains this queue itself once the current `process_event()` returns
(`Sml::process_queue<std::queue>` on every machine). **Never call `process_event()` from inside an
action.**

### Ownership & lifetime

- `CallManager` owns every `CallSession` via `unordered_map<string, unique_ptr<CallSession>>`. A
  session **cannot delete itself from inside its own SM action** (the SM is still executing on the
  stack) — `CallManager::schedule_remove()` removes it from every lookup path immediately, and
  `purge_scheduled()` actually destroys it once PJSIP dispatch has returned. Don't replace this
  with an immediate `erase()`/`reset()` from inside a session's own action.
- In `CallSession`, `setup_actions_`/`dialog_actions_` are declared **before**
  `setup_sm_`/`dialog_sm_` — the runners hold references into the actions objects, so actions must
  outlive (and therefore precede) the runners. Don't reorder these members. The same
  outlive/precede pattern applies to `SmRunner`'s internal `SmLogger`.
- `MediaBridge` is `enable_shared_from_this` and held via `shared_ptr` because it does async
  self-referencing work on the RTP thread — don't casually change this to `unique_ptr` or a raw
  pointer.
- PJSIP-owned objects (`pjsip_inv_session*` on `CallSession`) are non-owning raw pointers — PJSIP,
  not `CallSession`, owns their lifetime.
- pjsip clears `inv->last_answer` once the initial INVITE transaction confirms, so any later
  response (every re-INVITE response) must be built from the request's own `rdata` via
  `Inv::answer_request()`; `pjsip_inv_answer()` asserts. `ReinviteHandler` holds that `rdata` only
  for the duration of the SM dispatch.

### RTP

`OfferAnswerActions` binds each leg's socket (`MediaBridge::bind_leg_a()`/`bind_leg_b()`), sets
each remote endpoint as that leg's SDP answer arrives, configures both legs' codecs
(`configure_legs()`, which is what transcodes when the two legs differ) and starts relaying
(`start_bridge_loop()`). `ReinviteHandler` retargets a leg and reconfigures codecs live on a
re-INVITE. From `start_bridge_loop()` on, the relay runs on the `io_context` thread.

### Test coverage

Unit tests (`ctest`) cover the four machines with mocked actions (`src/sip/sm/tests/`), `Sdp`,
`extract_utils`, `RoutesManager`, `ReinviteHandler::media_changed`, and the RTP/transcoding classes
(`src/net/rtp/tests/`). `PjsipStack`, `MessageRouter`, and the per-call adapters and handlers have
**no** unit coverage — verify changes there with `just test-b2bua` (live SIPp calls, see
`engine/tests/integration/b2bua/README.md`) or add tests.

## Control-plane

See `control-plane/backend/AGENTS.md` and `control-plane/frontend/AGENTS.md` for the Express API
and React SPA respectively — both already document their response-envelope convention and
project structure in detail; not duplicated here.
