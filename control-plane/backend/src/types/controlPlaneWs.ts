// Mirrors engine/src/protocols/control_plane_ws.hpp (SbcEngine::Protocols::WsEnvelope)
// and call_event.hpp — no codegen step, update this by hand if either header changes.

import { RegistrationEvent, SipUserSnapshot } from './sipRegistrar';
import { SipRouteSnapshot } from './sipRoutes';

export type WsMessageType = 'snapshot' | 'registration' | 'call_started' | 'call_updated' | 'call_terminated';

// Every timestamp is the engine's own clock (ISO-8601 UTC), never the time an
// event arrived here.
export interface CallStarted {
  sip_call_id: string;
  caller: string;
  callee: string;
  started_at: string;
}

export interface CallUpdated {
  sip_call_id: string;
  route?: string | null;
  codec?: string | null;
  updated_at: string;
}

export type CallTerminatedStatus = 'Success' | 'Failed' | 'No Route Available' | 'Blocked';

export interface CallTerminated {
  sip_call_id: string;
  status: CallTerminatedStatus;
  failure_reason?: string | null;
  ended_at: string;
  duration_seconds?: number | null;
}

// Tagged envelope for every message on the control-plane websocket channel.
// Exactly one payload field is populated per `type`.
export interface WsEnvelope {
  type: WsMessageType;
  routes_snapshot?: SipRouteSnapshot;
  users_snapshot?: SipUserSnapshot;
  registration?: RegistrationEvent;
  call_started?: CallStarted;
  call_updated?: CallUpdated;
  call_terminated?: CallTerminated;
  // Monotonically increasing per backend process, stamped on every
  // control-plane -> engine message so ControlPlaneClient::on_read() can
  // drop one that arrives out of order. Never set on the engine ->
  // control-plane registration direction.
  seq?: number;
}
