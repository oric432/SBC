# AGENTS.md — control-plane/frontend

Guidance for Claude Code when working in this directory. Read together with:
- `/SBC/AGENTS.md` — the overall SBC project (the C++ B2BUA engine this UI manages)
- `/SBC/control-plane/README.md` — full setup/dev doc for both backend and frontend
- `/SBC/control-plane/backend/AGENTS.md` — the API this app talks to

## What this is

React/Vite SPA for managing the SBC's SIP route table — the same B2BUA routing config the C++
engine (`engine/protocols/SipRoutes.hpp`) consumes. Talks only to `control-plane/backend`; it does
not touch the engine or Postgres directly.

## Stack

React 18, TypeScript, Vite 7, Tailwind + Radix/shadcn (`src/components/ui`), Redux Toolkit +
RTK Query, react-hook-form + zod, react-router-dom, sonner (toasts).

Apply the `vercel-react-best-practices` skill (`.agents/skills/vercel-react-best-practices/AGENTS.md`)
when writing or reviewing code here.

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

Route payload shapes come from the C++ engine (`engine/protocols/SipRoutes.hpp`), exported as
JSON Schema by `tools/schema_generator.cpp` into `schemas/b2bua/*.json`. `features/routes/types.ts`
hand-mirrors those types (see the comment at the top of that file) — there's no codegen, so if the
engine struct changes, this file needs a matching manual update, along with the backend's
`src/types/sipRoutes.ts` and Drizzle schema.

## Conventions

- Path alias `@` → `src/` (see `vite.config.ts` / `tsconfig.json`).
- Forms: `react-hook-form` + `zod` resolver (see `RouteFormDialog.tsx` for the pattern).
- Mutations: call `.unwrap()` inside try/catch, `toast.success`/`toast.error(getApiErrorMessage(e))`.
- UI primitives come from shadcn (`components.json` config) — add new ones via the shadcn CLI
  rather than hand-rolling Radix wrappers.
- `npm run lint` / `npm run format` before committing.
