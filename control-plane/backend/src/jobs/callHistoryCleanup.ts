import { and, inArray, isNotNull, lt, sql } from 'drizzle-orm';
import cron from 'node-cron';

import { env } from '../config/env';
import { db } from '../db/client';
import { calls } from '../db/schema';
import { logger } from '../utils/logger';

const BATCH_SIZE = 1000;

// Arbitrary, fixed key for this job's advisory lock -- a multi-instance guard so only one
// backend process runs a cleanup tick at a time. Shows up as this value in pg_locks.
const ADVISORY_LOCK_KEY = 4_820_230_001n;

// One batch, auto-committed as its own statement (no surrounding transaction) so autovacuum
// can reclaim dead tuples between iterations instead of after one giant transaction.
const deleteBatch = async (cutoff: Date): Promise<number> => {
  const staleIds = db
    .select({ id: calls.id })
    .from(calls)
    .where(and(isNotNull(calls.endedAt), lt(calls.endedAt, cutoff)))
    .limit(BATCH_SIZE);

  const deleted = await db.delete(calls).where(inArray(calls.id, staleIds)).returning({ id: calls.id });
  return deleted.length;
};

const runCleanupTick = async (): Promise<void> => {
  const lockResult = await db.execute<{ pg_try_advisory_lock: boolean }>(
    sql`SELECT pg_try_advisory_lock(${ADVISORY_LOCK_KEY})`,
  );
  if (!lockResult.rows[0]?.pg_try_advisory_lock) {
    logger.warn('Call history cleanup: another instance holds the advisory lock, skipping this tick');
    return;
  }

  try {
    logger.info('Call history cleanup: started');
    const startedAt = Date.now();
    const cutoff = new Date(Date.now() - env.callHistoryRetentionDays * 24 * 60 * 60 * 1000);

    let totalDeleted = 0;
    for (;;) {
      const deletedInBatch = await deleteBatch(cutoff);
      totalDeleted += deletedInBatch;
      if (deletedInBatch < BATCH_SIZE) break;
    }

    if (totalDeleted === 0) {
      logger.info('Call history cleanup: nothing to clean up');
    } else {
      logger.info(`Call history cleanup: deleted ${totalDeleted} row(s) in ${Date.now() - startedAt}ms`);
    }
  } catch (err) {
    logger.error('Call history cleanup: failed', err);
  } finally {
    await db.execute(sql`SELECT pg_advisory_unlock(${ADVISORY_LOCK_KEY})`);
  }
};

export const scheduleCallHistoryCleanup = (): void => {
  cron.schedule('0 3 * * *', () => {
    runCleanupTick().catch((err) => logger.error('Call history cleanup: unhandled error', err));
  });
};
