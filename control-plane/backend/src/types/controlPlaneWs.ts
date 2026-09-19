// Mirrors engine/src/protocols/control_plane_ws.hpp (SbcEngine::Protocols::WsEnvelope) —
// no codegen step, update this by hand if that header changes.

import { RegistrationEvent, SipUserSnapshot } from './sipRegistrar';
import { SipRouteSnapshot } from './sipRoutes';

export type WsMessageType = 'snapshot' | 'registration';

// Tagged envelope for every message on the control-plane websocket channel.
// Exactly one payload field is populated per `type`.
export interface WsEnvelope {
  type: WsMessageType;
  routes_snapshot?: SipRouteSnapshot;
  users_snapshot?: SipUserSnapshot;
  registration?: RegistrationEvent;
  // Monotonically increasing per backend process, stamped on every
  // control-plane -> engine message so ControlPlaneClient::on_read() can
  // drop one that arrives out of order. Never set on the engine ->
  // control-plane registration direction.
  seq?: number;
}
