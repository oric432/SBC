import { WebSocket, WebSocketServer } from 'ws';
import type { Server } from 'http';

import { getRouteSnapshot } from '../controllers/routesController';
import { logger } from '../utils/logger';

// The engine connects here instead of polling /api/b2bua/routes: on connect
// it gets a full snapshot, and broadcastSnapshot() pushes another one on any
// route mutation so a running engine picks up changes without a restart.
export const ENGINE_WS_PATH = '/ws/engine';

let wss: WebSocketServer | undefined;

const sendSnapshot = async (socket: WebSocket): Promise<void> => {
  const snapshot = await getRouteSnapshot();
  socket.send(JSON.stringify({ type: 'snapshot', routes_snapshot: snapshot }));
};

export const attachEngineChannel = (server: Server): void => {
  wss = new WebSocketServer({ server, path: ENGINE_WS_PATH });

  wss.on('connection', (socket) => {
    logger.info('engine connected on the control-plane websocket channel');

    sendSnapshot(socket).catch((err: unknown) => {
      logger.error(`failed to send initial route snapshot: ${(err as Error).message}`);
    });

    socket.on('close', () => logger.info('engine websocket disconnected'));
    socket.on('error', (err) => logger.error(`engine websocket error: ${err.message}`));
  });
};

// Fire-and-forget by design (see routesController's call sites): a stale
// snapshot on one connected engine until its next mutation-triggered push
// is not worth failing an otherwise-successful route write over.
export const broadcastSnapshot = async (): Promise<void> => {
  if (!wss) {
    return;
  }
  const openClients = [...wss.clients].filter((client) => client.readyState === WebSocket.OPEN);
  if (openClients.length === 0) {
    return;
  }

  try {
    const snapshot = await getRouteSnapshot();
    const payload = JSON.stringify({ type: 'snapshot', routes_snapshot: snapshot });
    for (const client of openClients) {
      client.send(payload);
    }
  } catch (err) {
    logger.error(`failed to broadcast route snapshot: ${(err as Error).message}`);
  }
};
