# Control Plane Backend

## Setup

```bash
npm install
cp .env-example .env        # adjust DATABASE_URL if needed
docker compose up -d        # start Postgres
npm run db:migrate          # apply schema
npm run db:seed             # seed default route
```

## Development

```bash
npm run dev      # build + run
npm run lint
npm run format
npm start
```

## Database (Postgres + Drizzle ORM)

- `docker compose up -d` / `docker compose down` — start/stop local Postgres (creds in `docker-compose.yml`)
- `npm run db:generate` — generate a migration from `src/db/schema.ts` after changing it
- `npm run db:migrate` — apply pending migrations
- `npm run db:push` — push schema directly without a migration file (dev only)
- `npm run db:seed` — seed the default route table
- `npm run db:studio` — open Drizzle Studio to browse tables

Schema lives in `src/db/schema.ts`; generated migrations land in `drizzle/`.
npx eslint .
npm start

## Database

- `npm run db:generate` after editing the schema, `npm run db:migrate` to apply, `npm run db:push`
  for a no-migration dev sync, `npm run db:studio` to browse.
- Local Postgres via `docker-compose.yml` (`sbc`/`sbc`/`sbc_control_plane`, port 5432).
