import { Request, Response } from 'express';
import { and, eq } from 'drizzle-orm';
import { StatusCodes } from 'http-status-codes';

import { db } from '../db/client';
import { routeRules, routeTables } from '../db/schema';
import { ConflictError, NotFoundError } from '../errors';
import { SipRouteRule, SipRouteSnapshot } from '../types/sipRoutes';
import { sendSuccess } from '../utils/apiResponse';

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
  codec: rule.codec,
});

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

export const getRoutes = async (_req: Request, res: Response) => {
  const table = await getDefaultTable();

  const rules = await db
    .select()
    .from(routeRules)
    .where(eq(routeRules.tableId, table.tableId))
    .orderBy(routeRules.priority);

  const snapshot: SipRouteSnapshot = {
    table_id: table.tableId,
    version: table.version,
    routes: Object.fromEntries(rules.map((rule) => [rule.priority, toRouteRule(rule)])),
  };

  sendSuccess(res, snapshot);
};

export const createRoute = async (req: Request, res: Response) => {
  const { priority, uri, sip_address, port, codec } = req.body;
  const table = await getDefaultTable();

  const [created] = await db
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

  sendSuccess(res, toRouteRule(created), StatusCodes.CREATED);
};

export const updateRoute = async (req: Request, res: Response) => {
  const currentPriority = Number(req.params.priority);
  const { priority, uri, sip_address, port, codec } = req.body;
  const table = await getDefaultTable();

  const [updated] = await db
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

  if (!updated) {
    throw new NotFoundError(`Route with priority ${currentPriority} not found`);
  }

  sendSuccess(res, toRouteRule(updated));
};

export const swapRoute = async (req: Request, res: Response) => {
  const currentPriority = Number(req.params.priority);
  const { targetPriority, uri, sip_address, port, codec } = req.body;
  const table = await getDefaultTable();

  const updated = await db.transaction(async (tx) => {
    const [sourceRow] = await tx
      .select()
      .from(routeRules)
      .where(and(eq(routeRules.tableId, table.tableId), eq(routeRules.priority, currentPriority)));

    if (!sourceRow) {
      throw new NotFoundError(`Route with priority ${currentPriority} not found`);
    }

    const [targetRow] = await tx
      .select()
      .from(routeRules)
      .where(and(eq(routeRules.tableId, table.tableId), eq(routeRules.priority, targetPriority)));

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

    return updatedSource;
  });

  sendSuccess(res, toRouteRule(updated));
};

export const deleteRoute = async (req: Request, res: Response) => {
  const priority = Number(req.params.priority);
  const table = await getDefaultTable();

  const [deleted] = await db
    .delete(routeRules)
    .where(and(eq(routeRules.tableId, table.tableId), eq(routeRules.priority, priority)))
    .returning();

  if (!deleted) {
    throw new NotFoundError(`Route with priority ${priority} not found`);
  }

  sendSuccess(res, undefined);
};
