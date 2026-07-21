// Mirrors engine/protocols/SipRoutes.hpp (SbcEngine::Protocols) and schemas/b2bua/*.json

export interface SipRouteRule {
  uri: string;
  sip_address: string;
  port: number;
  codec?: string | null;
}

export interface SipRouteSnapshot {
  table_id: string;
  version: number;
  routes: Record<string, SipRouteRule>;
}
