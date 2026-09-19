import { WebSocket, WebSocketServer } from 'ws';
import type { RawData } from 'ws';
import type { Server } from 'http';

import { applyRegistrationEvent } from '../controllers/registrationsController';
import { getRouteSnapshot } from '../controllers/routesController';
import { getUsersSnapshot } from '../controllers/sipUsersController';
import type { WsEnvelope } from '../types/controlPlaneWs';
import { logger } from '../utils/logger';

// The engine connects here instead of polling /api/b2bua/routes: on connect
// it gets a full snapshot (routes + SIP users), and broadcastSnapshot()
// pushes another one on any mutation to either table so a running engine
// picks up changes without a restart. The engine pushes "registration"
// messages the other way, applied to the sip_registrations mirror table.
export const ENGINE_WS_PATH = '/ws/engine';

let wss: WebSocketServer | undefined;

// Stamped on every outbound (control-plane -> engine) message so
// ControlPlaneClient::on_read() can drop one that arrives out of order (see
// engine/src/protocols/control_plane_ws.hpp). Only meaningful because
// outboundChain below serializes the DB read that produces each snapshot
// with the stamp -- two racing sends must not be able to read-then-stamp out
// of order.
let nextSeq = 1;

// A promise tail: each of these runs a task only once the previous one has
// fully settled, without holding up the caller. Used for both the outbound
// snapshot sends (the initial per-connection send and every
// broadcastSnapshot()) and the inbound registration events, so neither can
// apply out of order. Tasks are expected to catch their own errors --
// letting one reject here would wedge every later task behind it.
const makeChain = () => {
  let tail: Promise<void> = Promise.resolve();
  return (task: () => Promise<void>): void => {
    tail = tail.then(task);
  };
};
const enqueueOutbound = makeChain();
const enqueueInbound = makeChain();

const buildSnapshotMessage = async (): Promise<string> => {
  const [routes_snapshot, users_snapshot] = await Promise.all([getRouteSnapshot(), getUsersSnapshot()]);
  const envelope: WsEnvelope = { type: 'snapshot', routes_snapshot, users_snapshot, seq: nextSeq++ };
  return JSON.stringify(envelope);
};

const sendSnapshotTo = async (sockets: WebSocket[]): Promise<void> => {
  if (sockets.length === 0) {
    return;
  }
  try {
    const message = await buildSnapshotMessage();
    for (const socket of sockets) {
      socket.send(message);
    }
  } catch (err) {
    logger.error(`failed to send snapshot: ${(err as Error).message}`);
  }
};

const handleEngineMessage = (raw: RawData): void => {
  let envelope: WsEnvelope;
  try {
    envelope = JSON.parse(raw.toString()) as WsEnvelope;
  } catch (err) {
    logger.error(`engine websocket sent invalid JSON: ${(err as Error).message}`);
    return;
  }

  if (envelope.type !== 'registration' || !envelope.registration) {
    logger.error(`engine websocket sent an unrecognized message (type="${envelope.type}")`);
    return;
  }

  const event = envelope.registration;
  enqueueInbound(() =>
    applyRegistrationEvent(event).catch((err: unknown) => {
      logger.error(`failed to apply registration event: ${(err as Error).message}`);
    }),
  );
};

export const attachEngineChannel = (server: Server): void => {
  wss = new WebSocketServer({ server, path: ENGINE_WS_PATH });

  wss.on('connection', (socket) => {
    logger.info('engine connected on the control-plane websocket channel');

    enqueueOutbound(() => sendSnapshotTo([socket]));

    socket.on('message', handleEngineMessage);
    socket.on('close', () => logger.info('engine websocket disconnected'));
    socket.on('error', (err) => logger.error(`engine websocket error: ${err.message}`));
  });
};

// Fire-and-forget by design (see routesController's/sipUsersController's call
// sites): a stale snapshot on one connected engine until its next
// mutation-triggered push is not worth failing an otherwise-successful write
// over.
export const broadcastSnapshot = (): void => {
  if (!wss) {
    return;
  }
  const openClients = [...wss.clients].filter((client) => client.readyState === WebSocket.OPEN);
  enqueueOutbound(() => sendSnapshotTo(openClients));
};
