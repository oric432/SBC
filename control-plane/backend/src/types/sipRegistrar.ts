// Mirrors engine/src/protocols/sip_registrar.hpp (SbcEngine::Protocols) —
// no codegen step, update this by hand if that header changes.

// Wire shape sent to the engine (over the websocket channel) — carries ha1,
// never a plaintext password. Never send this to the browser.
export interface SipUser {
  username: string;
  realm: string;
  ha1: string;
  enabled: boolean;
}

export interface SipUserSnapshot {
  users: SipUser[];
}

// Wire shape received from the engine on a "registration" websocket message.
export interface RegistrationEvent {
  aor: string;
  contact_uri: string;
  source_address: string;
  source_port: number;
  transport: string;
  user_agent?: string | null;
  expires_in_s: number;
  removed: boolean;
}

// UI-facing shape — deliberately excludes ha1.
export interface SipUserPublic {
  id: number;
  username: string;
  realm: string;
  enabled: boolean;
}
