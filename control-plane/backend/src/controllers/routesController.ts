import { Request, Response } from 'express';
import { and, asc, eq, or, sql } from 'drizzle-orm';
import { StatusCodes } from 'http-status-codes';

import { db } from '../db/client';
import { routeRules, routeTables } from '../db/schema';
import { ConflictError, NotFoundError } from '../errors';
import { SipRouteRule, SipRouteSnapshot, SupportedCodec } from '../types/sipRoutes';
import { sendSuccess } from '../utils/apiResponse';
import { logger } from '../utils/logger';
import { broadcastSnapshot } from '../ws/engineChannel';

const DEFAULT_TABLE_ID = 'default';

const toRouteRule = (rule: {
  uri: string;
  sipAddress: string;
  port: number;
  codec: string | null;
}): SipRouteRule => ({
  uri: rule.uri,
  sip_address: rule.sipAddress,
  port: rule.port,
  // The DB column is plain TEXT (no Postgres enum), so Drizzle's row type is
  // an unconstrained string — the enum is enforced at the write boundary
  // (routeBodySchema's zod validation), not the schema, so cast rather than
  // widen SipRouteRule's own type.
  codec: rule.codec as SupportedCodec | null,
});

// route_tables.version exists so a reader (the engine, or anyone diffing two
// snapshots) can tell two tables apart without comparing every row -- it has
// to move on every write, atomically with that write, or it's just a
// permanent "1" that says nothing. sql`... + 1` rather than read-then-write
// avoids losing an increment to two concurrent mutations.
type Tx = Parameters<Parameters<typeof db.transaction>[0]>[0];
const bumpTableVersion = async (tx: Tx, tableId: string): Promise<number> => {
  const [row] = await tx
    .update(routeTables)
    .set({ version: sql`${routeTables.version} + 1` })
    .where(eq(routeTables.tableId, tableId))
    .returning();
  return row.version;
};

const getDefaultTable = async () => {
  const [table] = await db
    .select()
    .from(routeTables)
    .where(eq(routeTables.tableId, DEFAULT_TABLE_ID));

  if (!table) {
    throw new NotFoundError(`Route table '${DEFAULT_TABLE_ID}' not found`);
  }

  return table;
};

// Shared by the GET /api/b2bua/routes debug read and the engine websocket
// channel (ws/engineChannel.ts), which is what the engine itself uses now.
export const getRouteSnapshot = async (): Promise<SipRouteSnapshot> => {
  const table = await getDefaultTable();

  const rules = await db
    .select()
    .from(routeRules)
    .where(eq(routeRules.tableId, table.tableId))
    .orderBy(routeRules.priority);

  return {
    table_id: table.tableId,
    version: table.version,
    routes: Object.fromEntries(rules.map((rule) => [rule.priority, toRouteRule(rule)])),
  };
};

export const getRoutes = async (_req: Request, res: Response) => {
  sendSuccess(res, await getRouteSnapshot());
};

export const createRoute = async (req: Request, res: Response) => {
  const { priority, uri, sip_address, port, codec } = req.body;
  const table = await getDefaultTable();

  const created = await db.transaction(async (tx) => {
    const [row] = await tx
      .insert(routeRules)
      .values({
        tableId: table.tableId,
        priority,
        uri,
        sipAddress: sip_address,
        port,
        codec: codec ?? null,
      })
      .returning();
    await bumpTableVersion(tx, table.tableId);
    return row;
  });

  logger.info(`Route created: priority=${created.priority} uri=${created.uri}`);
  void broadcastSnapshot();

  sendSuccess(res, toRouteRule(created), StatusCodes.CREATED);
};

export const updateRoute = async (req: Request, res: Response) => {
  const currentPriority = Number(req.params.priority);
  const { priority, uri, sip_address, port, codec } = req.body;
  const table = await getDefaultTable();

  const updated = await db.transaction(async (tx) => {
    const [row] = await tx
      .update(routeRules)
      .set({
        priority,
        uri,
        sipAddress: sip_address,
        port,
        codec: codec ?? null,
      })
      .where(and(eq(routeRules.tableId, table.tableId), eq(routeRules.priority, currentPriority)))
      .returning();

    if (!row) {
      throw new NotFoundError(`Route with priority ${currentPriority} not found`);
    }
    await bumpTableVersion(tx, table.tableId);
    return row;
  });

  const priorityLabel =
    updated.priority !== currentPriority ? `${currentPriority}->${updated.priority}` : `${updated.priority}`;
  logger.info(`Route updated: priority=${priorityLabel} uri=${updated.uri}`);
  void broadcastSnapshot();

  sendSuccess(res, toRouteRule(updated));
};

export const swapRoute = async (req: Request, res: Response) => {
  const currentPriority = Number(req.params.priority);
  const { targetPriority, uri, sip_address, port, codec } = req.body;
  const table = await getDefaultTable();

  const updated = await db.transaction(async (tx) => {
    // Lock both rows in one query, ordered by id ascending, so concurrent
    // swaps always acquire row locks in the same canonical order and can't
    // deadlock against each other.
    const rows = await tx
      .select()
      .from(routeRules)
      .where(
        and(
          eq(routeRules.tableId, table.tableId),
          or(eq(routeRules.priority, currentPriority), eq(routeRules.priority, targetPriority)),
        ),
      )
      .orderBy(asc(routeRules.id))
      .for('update');

    const sourceRow = rows.find((row) => row.priority === currentPriority);
    const targetRow = rows.find((row) => row.priority === targetPriority);

    if (!sourceRow) {
      throw new NotFoundError(`Route with priority ${currentPriority} not found`);
    }

    if (!targetRow) {
      throw new ConflictError(`Route with priority ${targetPriority} no longer exists`);
    }

    // Route past the unique(tableId, priority) constraint: park the target
    // row on a priority no live row can hold (negative), then move both
    // rows into their final spots.
    const tempPriority = -targetRow.id;
    await tx.update(routeRules).set({ priority: tempPriority }).where(eq(routeRules.id, targetRow.id));

    const [updatedSource] = await tx
      .update(routeRules)
      .set({
        priority: targetPriority,
        uri,
        sipAddress: sip_address,
        port,
        codec: codec ?? null,
      })
      .where(eq(routeRules.id, sourceRow.id))
      .returning();

    await tx.update(routeRules).set({ priority: currentPriority }).where(eq(routeRules.id, targetRow.id));

    await bumpTableVersion(tx, table.tableId);
    return updatedSource;
  });

  logger.info(`Route priorities swapped: ${currentPriority}<->${targetPriority}`);
  void broadcastSnapshot();

  sendSuccess(res, toRouteRule(updated));
};

export const deleteRoute = async (req: Request, res: Response) => {
  const priority = Number(req.params.priority);
  const table = await getDefaultTable();

  const deleted = await db.transaction(async (tx) => {
    const [row] = await tx
      .delete(routeRules)
      .where(and(eq(routeRules.tableId, table.tableId), eq(routeRules.priority, priority)))
      .returning();

    if (!row) {
      throw new NotFoundError(`Route with priority ${priority} not found`);
    }
    await bumpTableVersion(tx, table.tableId);
    return row;
  });

  logger.info(`Route deleted: priority=${deleted.priority} uri=${deleted.uri}`);
  void broadcastSnapshot();

  sendSuccess(res, undefined);
};
