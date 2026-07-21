npm install
npx eslint .
npm start

## Database

- `npm run db:generate` after editing the schema, `npm run db:migrate` to apply, `npm run db:push`
  for a no-migration dev sync, `npm run db:studio` to browse.
- Local Postgres via `docker-compose.yml` (`sbc`/`sbc`/`sbc_control_plane`, port 5432).