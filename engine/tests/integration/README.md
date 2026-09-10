# Engine integration tests

pytest-based integration tests that run the real `SbcEngine` binary and drive
it with an actual protocol client (SIPp for SIP/RTP), as opposed to
`engine/tests/*.cpp`, which exercises state machines via mocked actions.

## Shared fixtures (`conftest.py`)

- `engine_binary` - path to `build/SbcEngine`; fails with a clear message if
  it hasn't been built yet (`just build-dev`).
- `routes_stub_server` - a tiny in-process HTTP server standing in for the
  control-plane's `GET /api/b2bua/routes` endpoint. The engine blocks at
  startup fetching its route table over HTTP and retries forever on failure
  (it never crashes on connection-refused), so a real call flow needs
  *something* answering that endpoint; running the actual control-plane
  (Postgres + Node backend) just to serve one static route would be a heavy,
  unrelated CI dependency, so this stub serves the same JSON contract
  instead. See `src/protocols/{Api,SipRoutes}.hpp` for that contract if it
  ever changes.
- `sbc_engine` - session-scoped: writes a minimal `settings.toml` pointing at
  the stub, starts `SbcEngine`, waits for it to log a successful route load
  (the one reliable "ready" signal, since SIP itself is UDP), and tears it
  down at the end of the session. Exposes `.log_lines` / `.has_error_logs()`
  / `.log_tail()` for tests that want to assert on engine-side behavior.
- `initial_routes` - override this fixture in a suite's own `conftest.py` to
  seed the routes the stub serves before the engine starts.

A suite adding new scenario types (e.g. registration, media failure) should
extend the *assertion* side via `EngineHandle` rather than re-deriving engine
startup — that part is meant to be reused as-is.

See `b2bua/README.md` for the concrete B2BUA suite built on top of this.
