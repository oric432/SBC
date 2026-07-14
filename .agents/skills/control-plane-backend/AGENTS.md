# AGENTS.md — control-plane/backend

Guidance for Claude Code when working in this directory. Read together with:
- `SBC/AGENTS.md` — the overall SBC project (the C++ B2BUA engine this backend manages)
- `SBC/control-plane/README.md` — full setup/dev doc for both backend and frontend

## What this is

Express/TypeScript API backing the SBC control-plane web UI. It owns the SIP route table (the
B2BUA routing config the C++ engine reads) in Postgres, and exposes it to the frontend over REST.
It does not talk to the engine directly (no shared process, no RPC) — it is the source of truth
the engine's config loader/route table is expected to read from, matching the schemas below.

## Stack

Express 4, TypeScript (NodeNext modules), Drizzle ORM, `pg`, Zod validation, `http-status-codes`.

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

The route payload shape (`SipRouteRule`, `SipRouteSnapshot`) is defined by the JSON Schema files in
`schemas/b2bua/*.json` (`sip_route_rule.json`, `sip_route_snapshot.json`, `sip_route_update.json`).
`src/types/sipRoutes.ts` hand-mirrors those schemas — there is no codegen step, so if
`schemas/b2bua/*.json` changes, update `src/types/sipRoutes.ts` (and the Drizzle schema / frontend
`features/routes/types.ts`) to match by hand. Check `schemas/b2bua/*.json` when in doubt about
field names, optionality, or int ranges.

## Conventions

- Single quotes, 2-space indent, 120-col width (`.prettierrc`) — run `npm run format`.
- `strictNullChecks` + `noImplicitAny` on; `any` is allowed by lint config (`no-explicit-any` is
  off) but prefer typed where easy.
- Controllers stay thin: query via `db`, map rows to the response type, `sendSuccess`/throw an
  error class. Business logic doesn't belong in route files.
