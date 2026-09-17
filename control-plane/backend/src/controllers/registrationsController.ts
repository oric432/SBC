import { Request, Response } from 'express';
import { and, eq } from 'drizzle-orm';

import { db } from '../db/client';
import { sipRegistrations } from '../db/schema';
import { RegistrationEvent } from '../types/sipRegistrar';
import { sendSuccess } from '../utils/apiResponse';

export const getRegistrations = async (_req: Request, res: Response) => {
  const rows = await db.select().from(sipRegistrations);
  sendSuccess(res, rows);
};

// Called by ws/engineChannel.ts on every "registration" message -- this
// table is a mirror the engine drives directly, not written through the
// HTTP API at all.
export const applyRegistrationEvent = async (event: RegistrationEvent): Promise<void> => {
  if (event.removed) {
    await db
      .delete(sipRegistrations)
      .where(and(eq(sipRegistrations.aor, event.aor), eq(sipRegistrations.contactUri, event.contact_uri)));
    return;
  }

  const expiresAt = new Date(Date.now() + event.expires_in_s * 1000);
  await db
    .insert(sipRegistrations)
    .values({
      aor: event.aor,
      contactUri: event.contact_uri,
      sourceAddress: event.source_address,
      sourcePort: event.source_port,
      transport: event.transport,
      userAgent: event.user_agent ?? null,
      expiresAt,
    })
    .onConflictDoUpdate({
      target: [sipRegistrations.aor, sipRegistrations.contactUri],
      set: {
        sourceAddress: event.source_address,
        sourcePort: event.source_port,
        transport: event.transport,
        userAgent: event.user_agent ?? null,
        expiresAt,
        updatedAt: new Date(),
      },
    });
};
