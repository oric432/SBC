import { boolean, integer, pgTable, serial, text, timestamp, unique } from 'drizzle-orm/pg-core';

export const routeTables = pgTable('route_tables', {
  tableId: text('table_id').primaryKey(),
  version: integer('version').notNull().default(1),
});

export const routeRules = pgTable(
  'route_rules',
  {
    id: serial('id').primaryKey(),
    tableId: text('table_id')
      .notNull()
      .references(() => routeTables.tableId, { onDelete: 'cascade' }),
    priority: integer('priority').notNull(),
    uri: text('uri').notNull(),
    sipAddress: text('sip_address').notNull(),
    port: integer('port').notNull(),
    codec: text('codec'),
  },
  (table) => [unique().on(table.tableId, table.priority)],
);

// ha1 = MD5(username:realm:password) -- the plaintext password is never
// stored (see sipUsersController.computeHa1). username+realm together are
// this row's identity; there's no update path that changes either, since
// doing so without the plaintext password would leave a stale, unusable ha1.
export const sipUsers = pgTable(
  'sip_users',
  {
    id: serial('id').primaryKey(),
    username: text('username').notNull(),
    realm: text('realm').notNull(),
    ha1: text('ha1').notNull(),
    enabled: boolean('enabled').notNull().default(true),
  },
  (table) => [unique().on(table.username, table.realm)],
);

// Best-effort mirror of the engine's in-memory BindingStore, driven entirely
// by "registration" websocket events from the engine (ws/engineChannel.ts) --
// nothing in the HTTP API writes to this table. expiresAt makes a missed
// event self-describing (a stale-but-labelled row) rather than a phantom
// registration; see registrationsController.applyRegistrationEvent.
export const sipRegistrations = pgTable(
  'sip_registrations',
  {
    id: serial('id').primaryKey(),
    aor: text('aor').notNull(),
    contactUri: text('contact_uri').notNull(),
    sourceAddress: text('source_address').notNull(),
    sourcePort: integer('source_port').notNull(),
    transport: text('transport').notNull(),
    userAgent: text('user_agent'),
    expiresAt: timestamp('expires_at', { withTimezone: true }).notNull(),
    updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (table) => [unique().on(table.aor, table.contactUri)],
);
