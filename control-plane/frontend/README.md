# SBC Control Plane

Web control plane for SBC: React/Vite frontend + Express/Postgres backend for managing SIP routes.

## Stack

- **Frontend**: React 18, TypeScript, Vite 7, Tailwind, Radix/shadcn, Redux Toolkit, React Router
- **Backend**: Express, TypeScript, Drizzle ORM, PostgreSQL, Socket.IO
- **DB**: Postgres 16 (via Docker Compose)

## Prerequisites

- **Node.js >= 20.19** (Vite 7 requirement; Node 22 LTS recommended)
- **npm** (bundled with Node)
- **Docker** + Docker Compose (for local Postgres)

## Project Structure

```
control-plane/
├── backend/     # Express API, Drizzle ORM, Postgres
└── frontend/    # React + Vite SPA
```

## Setup

### Backend

```bash
cd backend
npm install
cp .env-example .env        # adjust DATABASE_URL if needed
docker compose up -d        # start Postgres (port 5432)
npm run db:migrate          # apply schema
npm run db:seed             # seed default route
npm run dev                 # build + run on http://localhost:3001
```

### Frontend

```bash
cd frontend
npm install
npm run dev                 # http://localhost:5173, proxies /api -> :3001
```

## Development

| Location | Command | Description |
|----------|---------|-------------|
| backend  | `npm run dev` | build + run |
| backend  | `npm start` | build + run (prod-style) |
| backend  | `npm run lint` / `npm run format` | ESLint check / fix |
| frontend | `npm run dev` | Vite dev server |
| frontend | `npm run build` | typecheck + production build |
| frontend | `npm run preview` | preview production build |
| frontend | `npm run lint` | ESLint |
| frontend | `npm run format` | Prettier |

## Backend Database (Postgres + Drizzle ORM)

- `docker compose up -d` / `docker compose down` — start/stop local Postgres (creds in `backend/docker-compose.yml`)
- `npm run db:generate` — generate a migration from `backend/src/db/schema.ts` after changing it
- `npm run db:migrate` — apply pending migrations
- `npm run db:push` — push schema directly without a migration file (dev only)
- `npm run db:seed` — seed the default route table
- `npm run db:studio` — open Drizzle Studio to browse tables

Schema lives in `backend/src/db/schema.ts`; generated migrations land in `backend/drizzle/`.

## Backend Environment Variables (`backend/.env`)

| Variable | Default | Description |
|----------|---------|-------------|
| `DATABASE_URL` | `postgresql://sbc:sbc@localhost:5432/sbc_control_plane` | Postgres connection string |
| `PORT` | `3001` | API server port |
| `NODE_ENV` | `development` | Node environment |
| `FRONTEND_URL` | `http://localhost:5173` | Allowed CORS origin |

## Frontend Notes

- Path alias `@` -> `frontend/src`
- Dev server proxies `/api` -> `http://localhost:3001` and `/socket.io` -> `http://localhost:3000` (see `frontend/vite.config.ts`)
- UI: Tailwind + Radix/shadcn components in `src/components/ui`
- State: Redux Toolkit; forms: react-hook-form + zod; API: axios; toasts: sonner
