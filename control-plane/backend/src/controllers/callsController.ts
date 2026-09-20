import { Request, Response } from 'express';
import { and, asc, desc, eq, inArray, isNotNull, isNull, sql } from 'drizzle-orm';

import { db } from '../db/client';
import { calls } from '../db/schema';
import type { CallStarted, CallTerminated, CallUpdated } from '../types/controlPlaneWs';
import { sendSuccess } from '../utils/apiResponse';
import { logger } from '../utils/logger';
import type { CallHistoryQuery } from '../schemas/callsSchemas';

// A call with no "terminated" event this long after it started is almost
// certainly one whose event was lost (engine crash) rather than a live call.
// Only flagged, never deleted -- see getActiveCalls.
const STALE_AFTER_MS = 6 * 60 * 60 * 1000;

type CallRow = typeof calls.$inferSelect;

const sortColumns = {
  timestamp: calls.startedAt,
  caller: calls.caller,
  callee: calls.callee,
} as const;

const toHistoryRecord = (row: CallRow) => ({
  id: String(row.id),
  sipCallId: row.sipCallId,
  caller: row.caller,
  callee: row.callee,
  route: row.route,
  codec: row.codec,
  status: row.status,
  timestamp: row.startedAt.toISOString(),
  durationSeconds: row.durationSeconds ?? undefined,
  failureReason: row.failureReason ?? undefined,
});

const toActiveCall = (row: CallRow) => ({
  id: String(row.id),
  sipCallId: row.sipCallId,
  caller: row.caller,
  callee: row.callee,
  route: row.route,
  codec: row.codec,
  startedAt: row.startedAt.toISOString(),
  answeredAt: row.answeredAt?.toISOString() ?? null,
  stale: Date.now() - row.startedAt.getTime() > STALE_AFTER_MS,
});

export const getCallHistory = async (req: Request, res: Response) => {
  const { status, sortField, sortDir, page, pageSize } = req.query as unknown as CallHistoryQuery;

  const where = and(isNotNull(calls.endedAt), status.length > 0 ? inArray(calls.status, status) : undefined);
  const orderBy = sortDir === 'asc' ? asc(sortColumns[sortField]) : desc(sortColumns[sortField]);

  const [rows, totalCount] = await Promise.all([
    db
      .select()
      .from(calls)
      .where(where)
      .orderBy(orderBy, desc(calls.id))
      .limit(pageSize)
      .offset((page - 1) * pageSize),
    db.$count(calls, where),
  ]);

  sendSuccess(res, { rows: rows.map(toHistoryRecord), totalCount });
};

export const getActiveCalls = async (_req: Request, res: Response) => {
  const rows = await db.select().from(calls).where(isNull(calls.endedAt)).orderBy(desc(calls.startedAt));
  sendSuccess(res, rows.map(toActiveCall));
};

// Called by ws/engineChannel.ts, in the order the engine sent them. The engine
// buffers these across a disconnect and replays the one write that was in
// flight when the connection dropped, so every handler here has to be
// idempotent.
export const applyCallStarted = async (event: CallStarted): Promise<void> => {
  const startedAt = new Date(event.started_at);
  await db
    .insert(calls)
    .values({ sipCallId: event.sip_call_id, caller: event.caller, callee: event.callee, startedAt })
    .onConflictDoUpdate({
      target: calls.sipCallId,
      set: { caller: event.caller, callee: event.callee, startedAt },
    });
};

export const applyCallUpdated = async (event: CallUpdated): Promise<void> => {
  const updatedAt = new Date(event.updated_at);
  const updated = await db
    .update(calls)
    .set({
      route: event.route ?? undefined,
      codec: event.codec ?? undefined,
      // The first update is the answer; a later codec-change update must not
      // overwrite it.
      answeredAt: sql`COALESCE(${calls.answeredAt}, ${updatedAt})`,
    })
    .where(eq(calls.sipCallId, event.sip_call_id))
    .returning({ id: calls.id });

  if (updated.length === 0) {
    logger.warn(`call_updated for unknown call ${event.sip_call_id}`);
  }
};

export const applyCallTerminated = async (event: CallTerminated): Promise<void> => {
  const updated = await db
    .update(calls)
    .set({
      status: event.status,
      failureReason: event.failure_reason ?? null,
      endedAt: new Date(event.ended_at),
      durationSeconds: event.duration_seconds ?? null,
    })
    .where(eq(calls.sipCallId, event.sip_call_id))
    .returning({ id: calls.id });

  if (updated.length === 0) {
    logger.warn(`call_terminated for unknown call ${event.sip_call_id}`);
  }
};
