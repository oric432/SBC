import { db } from './client';
import { routeRules, routeTables } from './schema';

async function seed() {
  const tableId = 'default';

  await db
    .insert(routeTables)
    .values({ tableId, version: 1 })
    .onConflictDoNothing();

  await db
    .insert(routeRules)
    .values({
      tableId,
      routeKey: '1',
      uri: 'sip:callee@sbc.local',
      sipAddress: '127.0.0.1',
      port: 5080,
      codec: null,
    })
    .onConflictDoNothing();

  console.log('Seed complete.');
  process.exit(0);
}

seed().catch((err) => {
  console.error('Seed failed:', err);
  process.exit(1);
});
