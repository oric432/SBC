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

SIP features not currently supported by the engine: PRACK (100rel), UPDATE, session timers, REFER,
SIP forking, transcoding, ICE, SRTP, WebRTC.

## Engine architecture

This section covers the parts of the engine's design that aren't obvious from file names alone —
read it before touching `src/sip/` or `src/net/rtp/`. Build/style conventions live in
`engine/AGENTS.md`; this is architecture and invariants only.

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
    (see the comment in `MediaBridge.hpp`).
  - `MediaBridge::set_error_handler()` is explicitly **not** thread-safe against a running relay
    loop — it must be called before `start_bridge_loop()`, never after.
  - `RoutesStore`'s route-snapshot swap is mutex-guarded.
  - `RtpInactivityTimer`'s scan-pending flag is atomic.

### State machines (`src/sip/sm/`)

Three Boost.SML machines, each templated on an `Actions` type implementing the matching interface
in `isbc_actions.hpp` (`ISetupContext`, `IDialogContext`, `IOptionsContext`):

- **`SetupSm`** (`setup_sm.hpp`) — drives call setup end-to-end:
  `Idle → Routing → Calling → WaitingForAnswer → Ringing → WaitingForAck → Done`, with
  `Failed`/`Cancelled`/`TimedOut` off-ramps from nearly every state. Handles routing resolution,
  the outbound INVITE, ringing/accept/reject/timeout/cancel.
- **`DialogSm`** (`dialog_sm.hpp`) — the in-dialog phase once setup completes:
  `Active → Reinviting → WaitingForReinviteAck → Terminating → Terminated → DialogDone`. Handles
  BYE, re-INVITE (including collision → 491), ACK, and error-triggered termination.
- **`OptionsSm`** (`options_sm.hpp`) — trivial stateless-message responder (OPTIONS/INFO).

A generic offer-answer state machine was attempted (see old commit messages referencing
"offer-answer SM") but was **paused and reverted** due to conflicts with an in-flight refactor —
it does not exist in the current tree. Verify against `src/sip/sm/` directly rather than trusting
commit history or branch names for what state machines currently exist.

**Self-fire pattern (all three SMs)**: when an action can deterministically know the next event
before external code does (e.g. a routing lookup's outcome, or that the outbound INVITE was
actually sent), it does **not** re-entrantly call `process_event()`. Instead it takes a
`Sml::back::process<...>` queue parameter by value and calls that; boost::sml drains this queue
itself once the current `process_event()` returns (`Sml::process_queue<std::queue>` on the
machine). **Never call `process_event()` from inside an action.**

### Ownership & lifetime

- `CallManager` owns every `CallSession` via `unordered_map<string, unique_ptr<CallSession>>`. A
  session **cannot delete itself from inside its own SM action** (the SM is still executing on the
  stack) — `CallManager::schedule_remove()` removes it from every lookup path immediately, and
  `purge_scheduled()` actually destroys it once PJSIP dispatch has returned. Don't replace this
  with an immediate `erase()`/`reset()` from inside a session's own action.
- In `CallSession`, `setup_actions_`/`dialog_actions_` are declared **before**
  `setup_sm_`/`dialog_sm_` — the SM runners hold references into the actions objects, so actions
  must outlive (and therefore precede) the runners. Don't reorder these members. The same
  outlive/precede pattern applies to each runner's internal `SmLogger`.
- `MediaBridge` is `enable_shared_from_this` and held via `shared_ptr` because it does async
  self-referencing work on the RTP thread — don't casually change this to `unique_ptr` or a raw
  pointer.
- PJSIP-owned objects (`pjsip_inv_session*` on `CallSession`) are non-owning raw pointers — PJSIP,
  not `CallSession`, owns their lifetime.

### RTP

`RealSetupActions`/`RealDialogActions` bind each leg's socket via `MediaBridge::bind_leg_a()` /
`bind_leg_b()`, set the negotiated remote endpoint via `set_remote_leg_a()`/`set_remote_leg_b()`
as each leg's SDP answer arrives, then start relaying via `start_bridge_loop()`. From that point
the relay runs on the `io_context` thread (see Threading model above).

### Known test-coverage gaps

Per `engine/README.md`: only the state machines have unit coverage (mocked actions, Catch2,
`src/sip/tests/`). RTP packet parsing, SDP mangling/validation, `PjsipStack`, and `MessageRouter`
have **no** unit tests yet. Don't assume behavior here is verified by CI — changes touching them
need either a live-call check (see `engine/README.md`) or new tests.

## Control-plane

See `control-plane/backend/AGENTS.md` and `control-plane/frontend/AGENTS.md` for the Express API
and React SPA respectively — both already document their response-envelope convention and
project structure in detail; not duplicated here.
