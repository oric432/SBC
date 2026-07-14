# AGENTS.md — control-plane/frontend

Guidance for Claude Code when working in this directory. Read together with:
- `/home/ori/Projects/SBC/AGENTS.md` — the overall SBC project (the C++ B2BUA engine this UI manages)
- `/home/ori/Projects/SBC/control-plane/README.md` — full setup/dev doc for both backend and frontend
- `/home/ori/Projects/SBC/control-plane/backend/AGENTS.md` — the API this app talks to

## What this is

React/Vite SPA for managing the SBC's SIP route table — the same B2BUA routing config the C++
engine (`engine/protocols/SipRoutes.hpp`) consumes. Talks only to `control-plane/backend`; it does
not touch the engine or Postgres directly.

## Stack

React 18, TypeScript, Vite 7, Tailwind + Radix/shadcn (`src/components/ui`), Redux Toolkit +
RTK Query, react-hook-form + zod, react-router-dom, sonner (toasts).

## How to run

```bash
npm install
npm run dev      # http://localhost:5173
```

Requires `control-plane/backend` running on `:3001` — Vite dev server proxies `/api` there (and
`/socket.io` to `:3000`), see `vite.config.ts`. Backend now stays up and returns clean `503`s even
without Postgres, so the UI won't hard-crash if the DB isn't running — you'll just see the error
message surfaced in place of data (see Error Handling below).

## Build

```bash
npm run build     # tsc typecheck + vite build -> dist/
npm run preview   # serve the production build locally
```

No test runner configured yet. Verify UI changes by running `npm run dev` and exercising the flow
in a browser — don't rely on typecheck alone for behavior.

## Project structure

```
src/
├── main.tsx / App.tsx        # entry, providers (Redux store, router)
├── routes/                    # route tree / page-level routing
├── store/                      # Redux store setup
├── features/
│   └── routes/                 # the SIP routes feature (the only feature so far)
│       ├── api.ts               # RTK Query endpoints, envelope unwrapping
│       ├── types.ts             # payload types, mirrored from engine schemas
│       └── components/          # feature-scoped components
├── components/
│   ├── ui/                     # shadcn/Radix primitives — generated, don't hand-edit heavily
│   └── layout/                  # app shell/layout components
├── hooks/                      # shared hooks
└── lib/
    ├── utils.ts                 # cn() and misc helpers
    └── api.ts                    # ApiResponse envelope type + getApiErrorMessage()
```

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
