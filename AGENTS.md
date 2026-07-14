# AGENTS.md

Guidance for coding agents (any LLM tool) working in this repo.

## What this is

SIP SBC/B2BUA in C++26, low-level PJSIP C API + Boost.SML state machines for call orchestration.
B2BUA: owns inbound + outbound SIP dialogs, rewrites SDP, anchors RTP through itself.

## Build & Test

```bash
cmake --preset debug && cmake --build --preset build-debug   # clang/Ninja
cmake --preset vs-debug && cmake --build --preset vs-build-debug  # MSVC
cmake --build build --target format          # format (format.sh may fail to resolve preset in some envs)
clang-tidy -p build/compile_commands.json engine/**/*.cpp
ctest --test-dir build                        # Catch2 tests in tests/
./build/sbc
```

Other presets: `release`, `relwithdebinfo` (and `vs-` equivalents).

## Conventions

- Naming (enforced by `.clang-tidy`): types/classes `CamelCase`; functions/variables `lower_case`;
  class members `lower_case_` (trailing underscore); constants/enum values `kCamelCase`.
- Mark pure utility functions with no instance state `static`.
- Keep Boost.SML transition tables high-level orchestration only — no SIP parsing or low-level
  detail inside state machine actions; that belongs in separate utility functions.
- CMake: built-in commands lowercase, custom functions descriptively named, prefer `target_*`
  commands over property setters.
- Format and lint before committing (see commands above).

## Design Principles

- SIP transaction correctness lives in PJSIP — don't reimplement it.
- SBC orchestration logic lives in state machines (Boost.SML), not ad hoc control flow.
- All SIP messages route through a central manager (`CallManager`) — no per-message ad hoc dispatch.
- Clear ownership/lifecycle for `CallSession`, dialogs, and media streams.
- No over-engineering — prefer straightforward code over abstract patterns.

## Testing

Catch2 + CTest, `tests/` (parallel to `engine/`). Test state machine transitions and orchestration
behavior — PJSIP's own transaction handling doesn't need re-testing here.

## Known Limitations

Not currently supported: PRACK (100rel), UPDATE, session timers, REFER, SIP forking, transcoding,
ICE, SRTP, WebRTC.
