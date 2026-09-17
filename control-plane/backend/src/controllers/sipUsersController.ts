import { createHash } from 'crypto';
import { Request, Response } from 'express';
import { eq } from 'drizzle-orm';
import { StatusCodes } from 'http-status-codes';

import { db } from '../db/client';
import { sipUsers } from '../db/schema';
import { NotFoundError } from '../errors';
import { SipUserPublic, SipUserSnapshot } from '../types/sipRegistrar';
import { sendSuccess } from '../utils/apiResponse';
import { logger } from '../utils/logger';
import { broadcastSnapshot } from '../ws/engineChannel';

// Digest requires the server to hold this exact hash -- bcrypt/argon2 isn't
// an option, and the plaintext password is never persisted.
const computeHa1 = (username: string, realm: string, password: string): string =>
  createHash('md5').update(`${username}:${realm}:${password}`).digest('hex');

type SipUserRow = { id: number; username: string; realm: string; ha1: string; enabled: boolean };

const toPublicUser = (row: SipUserRow): SipUserPublic => ({
  id: row.id,
  username: row.username,
  realm: row.realm,
  enabled: row.enabled,
});

// Shared by the engine websocket channel (ws/engineChannel.ts), which is the
// only thing that ever sees ha1 outside this module.
export const getUsersSnapshot = async (): Promise<SipUserSnapshot> => {
  const rows = await db.select().from(sipUsers);
  return { users: rows.map((row) => ({ username: row.username, realm: row.realm, ha1: row.ha1, enabled: row.enabled })) };
};

export const getSipUsers = async (_req: Request, res: Response) => {
  const rows = await db.select().from(sipUsers);
  sendSuccess(res, rows.map(toPublicUser));
};

export const createSipUser = async (req: Request, res: Response) => {
  const { username, realm, password, enabled } = req.body;
  const ha1 = computeHa1(username, realm, password);

  const [created] = await db.insert(sipUsers).values({ username, realm, ha1, enabled }).returning();

  logger.info(`SIP user created: ${username}@${realm}`);
  void broadcastSnapshot();

  sendSuccess(res, toPublicUser(created), StatusCodes.CREATED);
};

export const updateSipUser = async (req: Request, res: Response) => {
  const id = Number(req.params.id);
  const { password, enabled } = req.body;

  const [current] = await db.select().from(sipUsers).where(eq(sipUsers.id, id));
  if (!current) {
    throw new NotFoundError(`SIP user ${id} not found`);
  }

  const [updated] = await db
    .update(sipUsers)
    .set({
      ha1: password ? computeHa1(current.username, current.realm, password) : current.ha1,
      enabled: enabled ?? current.enabled,
    })
    .where(eq(sipUsers.id, id))
    .returning();

  logger.info(`SIP user updated: ${current.username}@${current.realm}`);
  void broadcastSnapshot();

  sendSuccess(res, toPublicUser(updated));
};

export const deleteSipUser = async (req: Request, res: Response) => {
  const id = Number(req.params.id);

  const [deleted] = await db.delete(sipUsers).where(eq(sipUsers.id, id)).returning();
  if (!deleted) {
    throw new NotFoundError(`SIP user ${id} not found`);
  }

  logger.info(`SIP user deleted: ${deleted.username}@${deleted.realm}`);
  void broadcastSnapshot();

  sendSuccess(res, undefined);
};
