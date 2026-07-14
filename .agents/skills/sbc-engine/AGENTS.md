# AGENTS.md

Guidance for coding agents (any LLM tool) working in this repo.

## What this is

SBC (Session Border Controller) project: a SIP B2BUA plus a web control plane for managing its
routing table. Two components:

| Component | Path | What it does |
| --- | --- | --- |
| Engine | `engine/` | C++26 B2BUA core (PJSIP + Boost.SML). Owns both SIP legs of every call, rewrites SDP, anchors RTP. See `engine/README.md`. |
| Control-plane backend | `control-plane/backend/` | Express/TypeScript API, owns the SIP route table in Postgres. See its own `AGENTS.md`. |
| Control-plane frontend | `control-plane/frontend/` | React/Vite SPA for managing routes through the backend. See its own `AGENTS.md`. |

The engine fetches a snapshot of the route table from the control-plane backend over HTTP at
startup; the backend is the source of truth for routing config, the engine is a read-only
consumer. The route payload shape is defined in `schemas/b2bua/*.json` (generated from the C++
engine's `engine/protocols/SipRoutes.hpp` via `tools/schema_generator.cpp`) and hand-mirrored by
both the backend and frontend TypeScript types — check the schema files, not the C++ source, when
working on either control-plane app.

## Known Limitations

SIP features not currently supported by the engine: PRACK (100rel), UPDATE, session timers, REFER,
SIP forking, transcoding, ICE, SRTP, WebRTC.

## Component-specific guidance

C++ code conventions, build/test commands, and design principles for `engine/` will be added here
(or to a dedicated `engine/AGENTS.md`) later. Until then, see `engine/README.md` for layout and
configuration.
