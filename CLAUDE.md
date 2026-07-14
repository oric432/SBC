# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# SBC Project

A simplified SIP SBC/B2BUA project implemented in modern C++ using the low-level PJSIP C API and Boost.SML for state machine orchestration.

The project focuses on:
* SIP call orchestration
* B2BUA behavior
* SDP parsing and rewriting
* RTP anchoring / relay
* Dialog management
* Multi-destination routing
* Clean state-machine-driven architecture

This project intentionally keeps SIP transaction correctness inside PJSIP and uses higher-level state machines to model SBC call flow.

## Quick Start

```bash
# Configure and build (debug mode)
cmake --preset debug
cmake --build --preset build-debug

# Format code before committing
./format.sh

# Run static analysis
clang-tidy -p build/compile_commands.json src/**/*.cpp

# Run the binary
./build/sbc
```

---

## High Level Architecture

The SBC acts as a B2BUA (Back-to-Back User Agent).
Instead of forwarding SIP packets directly between endpoints, the SBC creates:
* an inbound SIP leg
* an outbound SIP leg

Example:
```text
Caller <---- inbound dialog ----> SBC <---- outbound dialog ----> Callee
```

The SBC owns both dialogs. This allows:
* SDP rewriting
* RTP anchoring
* NAT traversal
* topology hiding
* media control
* routing logic
* re-INVITE handling

---

## SIP vs SDP vs RTP

* **SIP**: Controls the signaling (INVITE, 180 Ringing, 200 OK, ACK, BYE, CANCEL, re-INVITE). Handles call setup and teardown.
* **SDP**: The media negotiation layer. Describes RTP IP address, port, codecs, media direction. The SBC rewrites SDP so RTP flows through the SBC.
* **RTP**: Carries the actual media (audio/video). Typical SBC flow uses RTP anchoring: `Caller <---- RTP ----> SBC <---- RTP ----> Callee`.

---

## Core Concepts

### Call Session
A CallSession represents one full SBC-controlled call. It owns:
* **Inbound Leg**: `Caller <-> SBC` (dialog, transaction, SDP, RTP info)
* **Outbound Leg**: `SBC <-> Destination` (created after routing)
* **Media Session / RTP Relay**: Media anchor endpoints

### Router / Manager
Every SIP message must go through a central router/manager. Responsibilities:
* Match SIP messages to existing CallSessions
* Create new CallSessions for initial INVITEs
* Reject unknown dialogs
* Dispatch events into state machines

**Rules:**
* Initial INVITE without dialog -> create CallSession
* In-dialog SIP message -> route to existing CallSession
* Unknown dialog -> send 481 Call/Transaction Does Not Exist

---

## State Machine Philosophy

The state machines model **high-level SBC orchestration flow**, NOT **low-level SIP transaction internals**.

PJSIP handles retransmissions, parser correctness, SIP transaction state, SIP dialog internals, timers, and transport handling.

The project state machines (powered by **Boost.SML**) model:
* call setup & routing
* media negotiation
* dialog lifecycle
* re-INVITE flow & termination

SDP/media logic should be actions, not separate state machines.

---

## 1. Setup State Machine

**Purpose**: `Initial INVITE -> confirmed SIP dialog`

| State | Event | Guard | Action (side effect) | New State |
| --- | --- | --- | --- | --- |
| `Idle` | `InviteReceived` | valid SIP INVITE | create call session, send 100 Trying, parse SDP | `ValidatingOffer` |
| `Idle` | `InviteReceived` | invalid SIP INVITE | send 400 Bad Request | `Failed` |
| `ValidatingOffer` | `OfferValid` | SDP/media acceptable | continue routing | `Routing` |
| `ValidatingOffer` | `OfferInvalid` | invalid/unsupported SDP | send 488 Not Acceptable Here | `Failed` |
| `Routing` | `RouteFound` | destination exists | create outbound leg, rewrite SDP, create outbound INVITE | `Calling` |
| `Routing` | `RouteFailed` | no route | send failure response | `Failed` |
| `Calling` | `InviteSent` | outbound INVITE sent | wait for response | `WaitingForAnswer` |
| `WaitingForAnswer` | `RingingReceived` | response is 180 | forward 180 Ringing | `Ringing` |
| `WaitingForAnswer` | `CallAccepted` | 200 OK + valid answer SDP | parse answer SDP, rewrite SDP, forward 200 OK | `WaitingForAck` |
| `WaitingForAnswer` | `CallAccepted` | invalid/missing answer SDP | send ACK then BYE to callee, send failure to caller | `Failed` |
| `WaitingForAnswer` | `CallRejected` | final error response | forward rejection | `Failed` |
| `WaitingForAnswer` | `CancelReceived` | caller cancelled before answer | send CANCEL | `Cancelling` |
| `Ringing` | `CallAccepted` | 200 OK + valid answer SDP | parse answer SDP, rewrite SDP, forward 200 OK | `WaitingForAck` |
| `Ringing` | `CallAccepted` | invalid/missing answer SDP | send ACK then BYE to callee, send failure to caller | `Failed` |
| `Ringing` | `CallRejected` | final error response | forward rejection | `Failed` |
| `Ringing` | `CancelReceived` | caller cancelled before answer | send CANCEL | `Cancelling` |
| `Cancelling` | `InviteTerminated`| received 487/final INVITE term | forward final response, cleanup | `Cancelled` |
| `WaitingForAck` | `AckReceived` | ACK matches dialog | forward ACK, start dialog state machine | `Established` |
| `WaitingForAck`| `AckTimeout` | ACK missing | terminate call | `Failed` |
| `Cancelled` | `Cleanup` | always | cleanup resources | `Done` |
| `Failed` | `Cleanup` | always | cleanup resources | `Done` |
| `Established` | `DialogStarted` | dialog confirmed | start dialog state machine | `Done` |

---

## 2. Dialog State Machine

**Purpose**: `Confirmed dialog -> call termination`

| State | Event | Guard | Action (side effect) | New State |
| --- | --- | --- | --- | --- |
| `Active` | `ByeReceived` | valid BYE | send 200 OK to BYE sender, forward BYE to other leg | `Terminating` |
| `Active` | `ReinviteReceived` | re-INVITE allowed + valid SDP | parse SDP, rewrite SDP, forward re-INVITE | `Reinviting` |
| `Active` | `ReinviteReceived`| invalid SDP | reject with 488 Not Acceptable Here | `Active` |
| `Active` | `CallError` | transport/media failure | terminate call | `Terminating` |
| `Reinviting` | `ReinviteAccepted`| received 200 OK + valid answer SDP| parse answer SDP, rewrite SDP, forward 200 OK | `WaitingForReinviteAck`|
| `Reinviting` | `ReinviteRejected`| error response | forward rejection | `Active` |
| `Reinviting` | `ReinviteReceived`| another re-INVITE pending | reject with 491 Request Pending | `Reinviting` |
| `Reinviting` | `ByeReceived` | valid BYE | send 200 OK, forward BYE | `Terminating` |
| `WaitingForReinviteAck`| `AckReceived` | ACK valid | forward ACK, commit media changes | `Active` |
| `WaitingForReinviteAck`| `AckTimeout` | ACK missing | terminate call | `Terminating` |
| `WaitingForReinviteAck`| `ByeReceived` | valid BYE | send 200 OK, forward BYE | `Terminating` |
| `Terminating` | `CallEnded` | BYE completed | cleanup call | `Terminated` |
| `Terminated` | `Cleanup` | always | release resources | `Done` |

---

## Initial Supported SIP Flow

Supported: INVITE, 180 Ringing, 200 OK, ACK, CANCEL, BYE, re-INVITE.
Not currently supported: PRACK, UPDATE, REFER, session timers, SIP forking, transcoding, ICE, SRTP, WebRTC.

---

## Project Structure

```
.
├── src/                    # Source code
│   ├── main.cpp           # Entry point
│   ├── pch.hpp            # Precompiled headers
│   └── [features]/        # Feature implementations (callsession, router, sdp, etc.)
├── tests/                 # Unit tests (TBD - use Catch2 or similar)
├── third_party/           # External dependencies
│   ├── boost-sml/         # State machine library headers
│   └── ...
├── cmake/                 # CMake utilities and configurations
├── CMakeLists.txt         # CMake build definition
├── CMakePresets.json      # Build presets (debug/release, clang/VS)
├── .clang-format          # Code formatting rules (C++26)
├── .clang-tidy            # Static analysis rules
├── .clangd                # Language server configuration
└── format.sh              # Formatting helper script
```

## Development Workflow

### 1. Build Variants
The project uses CMake presets for different build configurations:

| Preset | Compiler | Build Type | Use Case |
|--------|----------|-----------|----------|
| `debug` | clang | Debug | Development with symbols and no optimization |
| `release` | clang | Release | Production build with optimizations |
| `relwithdebinfo` | clang | RelWithDebInfo | Profiling with optimizations and symbols |
| `vs-debug` | MSVC | Debug | Visual Studio development |
| `vs-release` | MSVC | Release | Visual Studio production |
| `vs-relwithdebinfo` | MSVC | RelWithDebInfo | Visual Studio profiling |

```bash
# Clang-based (Linux/macOS)
cmake --preset debug && cmake --build --preset build-debug

# MSVC-based (Windows)
cmake --preset vs-debug && cmake --build --preset vs-build-debug

# Profile build
cmake --preset relwithdebinfo && cmake --build --preset build-relwithdebinfo
```

### 2. Code Formatting
Code must be formatted before committing. Formatting is enforced via `.clang-format`.

```bash
# Option 1: Use the convenience script
./format.sh

# Option 2: Direct CMake target (requires build directory)
cmake --build build --target format

# Option 3: Manual clang-format
clang-format -i src/**/*.{cpp,hpp}
```

### 3. Static Analysis
The project uses clang-tidy with strict naming conventions. Run before committing:

```bash
# Requires compile_commands.json (auto-generated in build/)
clang-tidy -p build/compile_commands.json src/**/*.cpp
```

**Naming conventions enforced by .clang-tidy:**
- **Classes/Types**: `CamelCase` (e.g., `CallSession`, `DialogState`)
- **Functions/Methods**: `lower_case` (e.g., `send_invite()`, `validate_sdp()`)
- **Variables**: `lower_case` (e.g., `local_port`, `remote_addr`)
- **Class Members**: `lower_case` with `_` suffix (e.g., `dialog_id_`)
- **Constants**: `kCamelCase` (e.g., `kDefaultTimeout`, `kMaxConnections`)
- **Enum Constants**: `kCamelCase` (e.g., `kStateIdle`, `kEventInviteReceived`)

### 4. Typical Edit→Build→Test Loop

```bash
# 1. Make code changes
# 2. Format
./format.sh

# 3. Build
cmake --build --preset build-debug

# 4. Check for lint issues
clang-tidy -p build/compile_commands.json src/**/*.cpp

# 5. Run unit tests (when available)
# cmake --build --preset build-debug --target test

# 6. Commit
git add -A && git commit -m "..."
```

## C++ Guidelines

### Modern C++ Features
* Use modern C++ (C++26 standard set in CMakeLists.txt)
* Prefer `std::array`, `std::vector`, and containers over C-style arrays
* Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) for resource management
* Use `auto` where the type is obvious from context

### Static Methods
Pure utility functions with no instance-related state must be marked `static`:
```cpp
class SdpParser {
  static SdpOffer parse(const std::string& sdp_text);  // ✓
  void validate_media(const MediaLine& line);           // ✗ should be non-static or take media_line param
};
```

### State Machines (Boost.SML)
Keep Boost.SML transition tables succinct and focused on orchestration. Don't mix low-level details:
```cpp
// ✓ Good: High-level transitions
struct SetupStateMachine {
  auto operator()() const {
    return make_transition_table(
      *"Idle"_s + event<InviteReceived> / [](auto& ctx) { ctx.create_session(); } == "ValidatingOffer"_s,
      "ValidatingOffer"_s + event<OfferValid> == "Routing"_s
    );
  }
};

// ✗ Avoid: Low-level SIP parser logic in state machine
// → Put parser logic in separate utility functions
```

### CMake Style
- All CMake built-in commands: lowercase (e.g., `fetchcontent_declare()`)
- Custom functions: descriptive names (e.g., `myproject_set_project_warnings()`)
- Use target-based approach (`target_*` commands) rather than property setters

## Design Principles

* **SIP transaction correctness**: Keep inside PJSIP (don't reimplement SIP logic)
* **SBC orchestration logic**: Inside project state machines (Boost.SML)
* **Message routing**: All SIP messages through central manager/router
* **Resource ownership**: Clear ownership and lifecycle for CallSession, dialogs, and media streams
* **No over-engineering**: Prefer clear, straightforward code over abstract patterns

## Testing (Future)

When adding tests:
- Use Catch2 or Google Test framework
- Place tests in `tests/` directory (parallel to `src/`)
- Run tests as part of CI/CD
- Test state machine transitions, not low-level SIP details (PJSIP handles that)

## Known Limitations & Future Work

**Not currently supported:**
- PRACK (100rel), UPDATE
- Session timers, REFER
- SIP forking
- Transcoding, ICE, SRTP, WebRTC
- PJSIP integration (in progress)

**Next steps for developers:**
1. Link against PJSIP library (WIP in CMakeLists.txt)
2. Implement CallSession, Router, and state machine actions
3. Add RTP anchoring/relay logic
4. Create comprehensive test suite
5. Performance optimization and profiling
