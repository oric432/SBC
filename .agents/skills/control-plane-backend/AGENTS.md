# AGENTS.md — control-plane/backend

Guidance for Claude Code when working in this directory. Read together with:
- `/home/ori/Projects/SBC/AGENTS.md` — the overall SBC project (the C++ B2BUA engine this backend manages)
- `/home/ori/Projects/SBC/control-plane/README.md` — full setup/dev doc for both backend and frontend

## What this is

Express/TypeScript API backing the SBC control-plane web UI. It owns the SIP route table (the
B2BUA routing config the C++ engine reads) in Postgres, and exposes it to the frontend over REST.
It does not talk to the engine directly (no shared process, no RPC) — it is the source of truth
the engine's config loader/route table is expected to read from, matching the schemas below.

## Stack

Express 4, TypeScript (NodeNext modules), Drizzle ORM, `pg`, Zod validation, `http-status-codes`.

Apply the `nodejs-best-practices` skill (`.agents/skills/nodejs-best-practices/SKILL.md`) when
writing or reviewing code here.

## How to run

```bash
npm install
cp .env-example .env        # adjust DATABASE_URL if needed
docker compose up -d        # start Postgres (port 5432)
npm run db:migrate          # apply schema
npm run db:seed             # seed default route table
npm run dev                 # tsc build + node dist/app.js, http://localhost:3001
```

`npm run dev` and `npm start` both do a full `tsc` build then run `dist/app.js` — there is no
watch/hot-reload; re-run after every change. `npm run lint` / `npm run format` before committing.

The server starts even if Postgres is unreachable — it logs a warning and DB-backed endpoints
return `503` until the DB comes up (see Error Handling below). Don't "fix" this by making startup
fail on DB-down; that's intentional.

## Build

```bash
npm run build     # tsc -p . -> dist/
```

No test runner configured yet (`npm test` is a stub). Verify changes via `tsc --noEmit` plus a
live curl against the running server.

## Project structure

```
src/
├── app.ts                    # express app wiring, CORS, error middleware, startup DB check
├── db/
│   ├── client.ts              # pg Pool + drizzle instance, checkDbConnection()
│   └── schema.ts               # drizzle table definitions (source of truth for DB shape)
├── controllers/                # request handlers, one per resource
├── routes/                     # express Routers, wire path -> controller
├── middlewares/
│   ├── errorHandlerMiddleware.ts   # last-resort handler, applies the response envelope
│   └── validation.ts               # zod-based validateBody/Params/Query
├── errors/                     # CustomAPIError subclasses (BadRequestError, NotFoundError, ...)
├── types/                      # request/response payload types, mirrored from engine schemas
└── utils/
    ├── apiResponse.ts           # sendSuccess/sendError — the response envelope, use these, not res.json()
    └── logger.ts
```

## API response envelope

Every endpoint returns one of these two shapes — always use `sendSuccess`/`sendError` from
`src/utils/apiResponse.ts`, never call `res.json()` directly:

```ts
{ success: true,  data: T }
{ success: false, error: { message: string, details?: unknown } }
```

Status codes always come from `http-status-codes` (`StatusCodes.OK`, `StatusCodes.NOT_FOUND`,
...) — never hardcode a numeric status. `NotFoundError` / `BadRequestError` / `UnauthorizedError`
/ `ServiceUnavailableError` (in `src/errors/`) each carry their own `statusCode`; `throw` them
from a controller and `errorHandlerMiddleware` turns them into the envelope automatically.

## Error handling

- `express-async-errors` is imported once in `app.ts` — plain `async` route handlers that throw
  are automatically forwarded to `errorHandlerMiddleware`. Don't add manual try/catch or a
  wrapper around controllers for this; it's already handled.
- `errorHandlerMiddleware` special-cases DB connectivity errors (raw `ECONNREFUSED`/`ENOTFOUND`/
  `ETIMEDOUT`, or the same wrapped inside a Drizzle `DrizzleQueryError.cause`) into a clean
  `503 Database unavailable` instead of leaking a stack trace to the client.
- `db/client.ts` has a `pool.on('error', ...)` guard — without it, an idle client's async error
  event is an uncaught exception that kills the process. Don't remove it.
- `checkDbConnection()` runs once at startup (non-fatal) to log whether Postgres is reachable.

## Database

- Schema: `src/db/schema.ts` (Drizzle). Two tables: `route_tables` (versioned route table
  metadata) and `route_rules` (individual routes, FK'd to `route_tables.table_id`).
- `npm run db:generate` after editing the schema, `npm run db:migrate` to apply, `npm run db:push`
  for a no-migration dev sync, `npm run db:studio` to browse.
- Local Postgres via `docker-compose.yml` (`sbc`/`sbc`/`sbc_control_plane`, port 5432).

## Protocol / schema source of truth

The route payload shape (`SipRouteRule`, `SipRouteSnapshot`) is defined in the C++ engine at
`engine/protocols/SipRoutes.hpp` and exported as JSON Schema via `tools/schema_generator.cpp` into
`schemas/b2bua/*.json` (`sip_route_rule.json`, `sip_route_snapshot.json`, `sip_route_update.json`).
`src/types/sipRoutes.ts` hand-mirrors those types — there is no codegen step, so if the engine
struct changes, update `src/types/sipRoutes.ts` (and the Drizzle schema / frontend
`features/routes/types.ts`) to match by hand. Check `schemas/b2bua/*.json` when in doubt about
field names, optionality, or int ranges.

## Conventions

- Single quotes, 2-space indent, 120-col width (`.prettierrc`) — run `npm run format`.
- `strictNullChecks` + `noImplicitAny` on; `any` is allowed by lint config (`no-explicit-any` is
  off) but prefer typed where easy.
- Controllers stay thin: query via `db`, map rows to the response type, `sendSuccess`/throw an
  error class. Business logic doesn't belong in route files.
