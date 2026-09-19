import { WebSocket, WebSocketServer } from 'ws';
import type { RawData } from 'ws';
import type { Server } from 'http';

import { applyRegistrationEvent } from '../controllers/registrationsController';
import { getRouteSnapshot } from '../controllers/routesController';
import { getUsersSnapshot } from '../controllers/sipUsersController';
import { RegistrationEvent } from '../types/sipRegistrar';
import { logger } from '../utils/logger';

// The engine connects here instead of polling /api/b2bua/routes: on connect
// it gets a full snapshot (routes + SIP users), and broadcastSnapshot()
// pushes another one on any mutation to either table so a running engine
// picks up changes without a restart. The engine pushes "registration"
// messages the other way, applied to the sip_registrations mirror table.
export const ENGINE_WS_PATH = '/ws/engine';

let wss: WebSocketServer | undefined;

const buildSnapshotMessage = async (): Promise<string> => {
  const [routes_snapshot, users_snapshot] = await Promise.all([getRouteSnapshot(), getUsersSnapshot()]);
  return JSON.stringify({ type: 'snapshot', routes_snapshot, users_snapshot });
};

const handleEngineMessage = (raw: RawData): void => {
  let envelope: { type?: string; registration?: RegistrationEvent };
  try {
    envelope = JSON.parse(raw.toString());
  } catch (err) {
    logger.error(`engine websocket sent invalid JSON: ${(err as Error).message}`);
    return;
  }

  if (envelope.type !== 'registration' || !envelope.registration) {
    logger.error(`engine websocket sent an unrecognized message (type="${envelope.type}")`);
    return;
  }

  applyRegistrationEvent(envelope.registration).catch((err: unknown) => {
    logger.error(`failed to apply registration event: ${(err as Error).message}`);
  });
};

export const attachEngineChannel = (server: Server): void => {
  wss = new WebSocketServer({ server, path: ENGINE_WS_PATH });

  wss.on('connection', (socket) => {
    logger.info('engine connected on the control-plane websocket channel');

    buildSnapshotMessage()
      .then((message) => socket.send(message))
      .catch((err: unknown) => {
        logger.error(`failed to send initial snapshot: ${(err as Error).message}`);
      });

    socket.on('message', handleEngineMessage);
    socket.on('close', () => logger.info('engine websocket disconnected'));
    socket.on('error', (err) => logger.error(`engine websocket error: ${err.message}`));
  });
};

// Fire-and-forget by design (see routesController's/sipUsersController's call
// sites): a stale snapshot on one connected engine until its next
// mutation-triggered push is not worth failing an otherwise-successful write
// over.
export const broadcastSnapshot = async (): Promise<void> => {
  if (!wss) {
    return;
  }
  const openClients = [...wss.clients].filter((client) => client.readyState === WebSocket.OPEN);
  if (openClients.length === 0) {
    return;
  }

  try {
    const message = await buildSnapshotMessage();
    for (const client of openClients) {
      client.send(message);
    }
  } catch (err) {
    logger.error(`failed to broadcast snapshot: ${(err as Error).message}`);
  }
};
