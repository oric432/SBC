# AGENTS.md — control-plane/frontend

Guidance to LLM when working in this directory. Read together with:
- `/SBC/AGENTS.md` — the overall SBC project (the C++ B2BUA engine this UI manages)
- `/SBC/control-plane/backend/README.md` — backend setup/dev docs
- `/SBC/control-plane/frontend/README.md` — frontend setup/dev docs
- `/SBC/control-plane/backend/AGENTS.md` — the API this app talks to

## What this is

React/Vite SPA for managing the SBC's SIP route table — the same B2BUA routing config the C++
engine (`engine/protocols/SipRoutes.hpp`) consumes. Talks only to `control-plane/backend`; it does
not touch the engine or Postgres directly.

## Stack

React 18, TypeScript, Vite 7, Tailwind + Radix/shadcn (`src/components/ui`), Redux Toolkit +
RTK Query, react-hook-form + zod, react-router-dom, sonner (toasts).

## Project structure

New features follow the same shape as `features/routes/`: an `api.ts` (RTK Query slice), a
`types.ts`, and a `components/` folder.

## Talking to the backend

Every backend endpoint returns an envelope (see `src/lib/api.ts`, mirrors backend
`src/utils/apiResponse.ts`):

```ts
{ success: true,  data: T }
{ success: false, error: { message: string, details?: unknown } }
```

- RTK Query endpoints unwrap the success case with `transformResponse` (see
  `features/routes/api.ts`) so hooks like `useGetRoutesQuery` return the payload type directly,
  not the envelope.
- On error, `fetchBaseQuery` never calls `transformResponse` — it puts the raw parsed body on
  `error.data` instead. Use `getApiErrorMessage(error)` from `src/lib/api.ts` to pull the real
  backend message out of `isError`/`error` (queries) or a caught `.unwrap()` rejection
  (mutations) — don't show a generic "failed" string when the backend already sent one.

## Protocol / schema source of truth

Route payload shapes are defined by the JSON Schema files in `schemas/b2bua/*.json`
(`sip_route_rule.json`, `sip_route_snapshot.json`, `sip_route_update.json`) — check those for field
names, optionality, and int ranges. `features/routes/types.ts` hand-mirrors those schemas (see the
comment at the top of that file) — there's no codegen, so if `schemas/b2bua/*.json` changes, this
file needs a matching manual update, along with the backend's `src/types/sipRoutes.ts` and Drizzle
schema.

## Conventions

- Path alias `@` → `src/` (see `vite.config.ts` / `tsconfig.json`).
- Forms: `react-hook-form` + `zod` resolver (see `RouteFormDialog.tsx` for the pattern).
- Mutations: call `.unwrap()` inside try/catch, `toast.success`/`toast.error(getApiErrorMessage(e))`.
- UI primitives come from shadcn (`components.json` config) — add new ones via the shadcn CLI
  rather than hand-rolling Radix wrappers.
- `npm run lint` / `npm run format` before committing.
