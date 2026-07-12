import { integer, pgTable, serial, text, unique } from 'drizzle-orm/pg-core';

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
    routeKey: text('route_key').notNull(),
    uri: text('uri').notNull(),
    sipAddress: text('sip_address').notNull(),
    port: integer('port').notNull(),
    codec: text('codec'),
  },
  (table) => [unique().on(table.tableId, table.routeKey)],
);
