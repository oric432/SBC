import { Request, Response } from 'express';
import { eq } from 'drizzle-orm';

import { db } from '../db/client';
import { routeRules, routeTables } from '../db/schema';
import { NotFoundError } from '../errors';
import { SipRouteSnapshot } from '../types/sipRoutes';
import { sendSuccess } from '../utils/apiResponse';

const DEFAULT_TABLE_ID = 'default';

export const getRoutes = async (_req: Request, res: Response) => {
  const [table] = await db
    .select()
    .from(routeTables)
    .where(eq(routeTables.tableId, DEFAULT_TABLE_ID));

  if (!table) {
    throw new NotFoundError(`Route table '${DEFAULT_TABLE_ID}' not found`);
  }

  const rules = await db
    .select()
    .from(routeRules)
    .where(eq(routeRules.tableId, table.tableId));

  const snapshot: SipRouteSnapshot = {
    table_id: table.tableId,
    version: table.version,
    routes: Object.fromEntries(
      rules.map((rule) => [
        rule.routeKey,
        {
          uri: rule.uri,
          sip_address: rule.sipAddress,
          port: rule.port,
          codec: rule.codec,
        },
      ]),
    ),
  };

  sendSuccess(res, snapshot);
};
