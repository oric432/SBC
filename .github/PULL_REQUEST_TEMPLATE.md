## What changed and why

<!-- One or two sentences. Link an issue if there is one. -->

## Checklist

- [ ] `just build-ci` and `just test-ci` pass locally (if `engine/` changed)
- [ ] `just format-check` passes (if `engine/` changed)
- [ ] `npm run lint` and `npm run build` pass locally (if `control-plane/backend` or `control-plane/frontend` changed)
- [ ] Touches SM transitions, `CallManager` lifetime, or `MediaBridge` ownership? Explain the invariant you preserved (see root `AGENTS.md`):
